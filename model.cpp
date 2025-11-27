#include "model.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>

// Minimal texture loader: If stb_image is available it will be used.
// Otherwise, we create a 1x1 fallback texture.
#ifdef __has_include
#  if __has_include("stb_image.h")
#    define HAS_STB 1
#    define STB_IMAGE_IMPLEMENTATION
#    include "stb_image.h"
#  else
#    define HAS_STB 0
#  endif
#else
#  define HAS_STB 0
#endif

// ---------------- Load Texture ----------------
static unsigned char* stb_try_load(const std::string& p, int* w, int* h, int* c) {
#if HAS_STB
    return stbi_load(p.c_str(), w, h, c, 0);
#else
    (void)p; (void)w; (void)h; (void)c; return nullptr;
#endif
}

GLuint loadTexture(const char* filePath) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    int width, height, nrChannels;
    unsigned char* data = nullptr;
    width = height = nrChannels = 0;

    // Try multiple relative locations to handle different working dirs
#if HAS_STB
    stbi_set_flip_vertically_on_load(true);
    std::string base(filePath);
    std::string cands[4] = { base, std::string("../")+base, std::string("../../")+base, std::string("../../../")+base };
    for (const auto& p : cands) {
        data = stb_try_load(p, &width, &height, &nrChannels);
        if (data) break;
    }
#endif

    if (data && width > 0 && height > 0) {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    #if HAS_STB
    stbi_image_free(data);
    #endif
    } else {
        // Fallback: 1x1 white texture
        std::cout << "Failed to load texture: " << filePath << ". Using fallback.\n";
        unsigned char white[4] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return texID;
}

// ---------------- Simple OBJ loader ----------------
// Only supports position, normal, texcoord, and single texture
Model loadModel(const char* path, const char* texturePath) {
    Model model;
    Mesh mesh;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    // Track bounding radius in XZ while parsing geometry
    float maxRadiusXZ = 0.0f;
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();

    // Try multiple relative paths for model file as well
    std::ifstream file(path);
    std::string openedPath = path;
    if (!file.is_open()) {
        std::string base(path);
        std::string cands[3] = { std::string("../")+base, std::string("../../")+base, std::string("../../../")+base };
        for (const auto& p : cands) {
            file.open(p);
            if (file.is_open()) { openedPath = p; break; }
        }
    }
    if (!file.is_open()) {
        std::cout << "Failed to open model: " << path << std::endl;
        return model;
    }

    std::vector<glm::vec3> temp_positions;
    std::vector<glm::vec3> temp_normals;
    std::vector<glm::vec2> temp_texcoords;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;
        if (prefix == "v") {
            glm::vec3 pos; ss >> pos.x >> pos.y >> pos.z;
            temp_positions.push_back(pos);
            float rXZ = std::sqrt(pos.x*pos.x + pos.z*pos.z);
            if (rXZ > maxRadiusXZ) maxRadiusXZ = rXZ;
            if (pos.y < minY) minY = pos.y;
            if (pos.y > maxY) maxY = pos.y;
        } else if (prefix == "vn") {
            glm::vec3 norm; ss >> norm.x >> norm.y >> norm.z;
            temp_normals.push_back(norm);
        } else if (prefix == "vt") {
            glm::vec2 uv; ss >> uv.x >> uv.y;
            temp_texcoords.push_back(uv);
        } else if (prefix == "f") {
            // Collect face tokens, handle v/vt/vn | v//vn | v/vt
            std::vector<std::string> tokens;
            std::string tok;
            while (ss >> tok) tokens.push_back(tok);
            if (tokens.size() < 3) continue;

            auto parseIdx = [&](const std::string& t, int& v, int& vt, int& vn){
                v = vt = vn = -1;
                size_t first = t.find('/');
                if (first == std::string::npos) {
                    v = std::stoi(t);
                    return;
                }
                size_t second = t.find('/', first+1);
                std::string sv = t.substr(0, first);
                std::string svt = (second==std::string::npos)? t.substr(first+1) : t.substr(first+1, second-first-1);
                std::string svn = (second==std::string::npos)? std::string("") : t.substr(second+1);
                if (!sv.empty()) v = std::stoi(sv);
                if (!svt.empty()) vt = std::stoi(svt);
                if (!svn.empty()) vn = std::stoi(svn);
            };

            auto resolveIndex = [](int idx, int size)->int{
                if (idx > 0) return idx;          // 1-based positive
                if (idx < 0) return size + idx + 1; // -1 means last
                return 0;
            };

            auto addVertex = [&](int vIdx, int tIdx, int nIdx, const glm::vec3& overrideNormal){
                glm::vec3 pos(0);
                glm::vec3 norm(0,1,0);
                glm::vec2 uv(0);
                vIdx = resolveIndex(vIdx, (int)temp_positions.size());
                tIdx = resolveIndex(tIdx, (int)temp_texcoords.size());
                nIdx = resolveIndex(nIdx, (int)temp_normals.size());

                if (vIdx>0 && vIdx <= (int)temp_positions.size()) pos = temp_positions[vIdx-1];
                if (nIdx>0 && nIdx <= (int)temp_normals.size()) norm = temp_normals[nIdx-1];
                else if (overrideNormal != glm::vec3(0)) norm = overrideNormal;
                if (tIdx>0 && tIdx <= (int)temp_texcoords.size()) uv = temp_texcoords[tIdx-1];

                vertices.push_back(pos.x);
                vertices.push_back(pos.y);
                vertices.push_back(pos.z);

                vertices.push_back(norm.x);
                vertices.push_back(norm.y);
                vertices.push_back(norm.z);

                vertices.push_back(uv.x);
                vertices.push_back(uv.y);

                indices.push_back((unsigned int)indices.size());
            };

            // Triangulate fan: (0, i-1, i)
            auto getPos = [&](int idx){ return (idx>0 && idx <= (int)temp_positions.size()) ? temp_positions[idx-1] : glm::vec3(0); };

            for (size_t i = 2; i < tokens.size(); ++i) {
                int v0=-1, vt0=-1, vn0=-1;
                int v1=-1, vt1=-1, vn1=-1;
                int v2=-1, vt2=-1, vn2=-1;
                parseIdx(tokens[0], v0, vt0, vn0);
                parseIdx(tokens[i-1], v1, vt1, vn1);
                parseIdx(tokens[i], v2, vt2, vn2);

                // Compute face normal if needed
                glm::vec3 n(0);
                if (vn0<0 || vn1<0 || vn2<0) {
                    glm::vec3 p0 = getPos(v0), p1 = getPos(v1), p2 = getPos(v2);
                    glm::vec3 e1 = p1 - p0;
                    glm::vec3 e2 = p2 - p0;
                    n = glm::normalize(glm::cross(e1, e2));
                    if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) n = glm::vec3(0,1,0);
                }

                addVertex(v0, vt0, vn0, n);
                addVertex(v1, vt1, vn1, n);
                addVertex(v2, vt2, vn2, n);
            }
        }
    }

    file.close();

    // Suppress verbose OBJ load logging; action logs are handled in main.cpp

    // Setup OpenGL buffers
    glGenVertexArrays(1, &mesh.VAO);
    glGenBuffers(1, &mesh.VBO);
    glGenBuffers(1, &mesh.EBO);

    glBindVertexArray(mesh.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0); // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float))); // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float))); // uv
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    mesh.indexCount = indices.size();
    mesh.textureID = loadTexture(texturePath);

    model.meshes.push_back(mesh);
    model.position = glm::vec3(0.0f);
    model.rotation = glm::vec3(0.0f);
    model.scale    = glm::vec3(1.0f);
    model.radiusXZ = (maxRadiusXZ > 0.0f ? maxRadiusXZ : 1.0f);
    if (!std::isfinite(minY)) { minY = 0.0f; }
    if (!std::isfinite(maxY)) { maxY = 0.0f; }
    model.minY = minY;
    model.maxY = maxY;

    return model;
}

// ---------------- Draw Model ----------------
void drawModel(const Model& model, const glm::mat4& view, const glm::mat4& projection) {
    extern GLuint shaderProgram;
    glUseProgram(shaderProgram);

    for (const Mesh& m : model.meshes) {
        glm::mat4 modelMat = glm::mat4(1.0f);
        modelMat = glm::translate(modelMat, model.position);
        modelMat = glm::rotate(modelMat, model.rotation.x, glm::vec3(1,0,0));
        modelMat = glm::rotate(modelMat, model.rotation.y, glm::vec3(0,1,0));
        modelMat = glm::rotate(modelMat, model.rotation.z, glm::vec3(0,0,1));
        modelMat = glm::scale(modelMat, model.scale);

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelMat[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &projection[0][0]);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m.textureID);
        glUniform1i(glGetUniformLocation(shaderProgram, "texture_diffuse1"), 0);

        glBindVertexArray(m.VAO);
        glDrawElements(GL_TRIANGLES, m.indexCount, GL_UNSIGNED_INT, 0);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
