#pragma once
#include <string>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


// ---------------- Mesh ----------------
struct Mesh {
    GLuint VAO;
    GLuint VBO;
    GLuint EBO;
    GLuint textureID;
    GLsizei indexCount;
};

// ---------------- Model ----------------
struct Model {
    std::vector<Mesh> meshes;
    glm::vec3 position; // world position
    glm::vec3 rotation; // Euler angles (radians) x=pitch, y=yaw, z=roll
    glm::vec3 scale;    // per-model scale
    float radiusXZ;     // bounding radius in XZ (for footprint-based scaling)
    float minY;         // lowest vertex Y in model space
    float maxY;         // highest vertex Y in model space
};

// ---------------- Functions ----------------
Model loadModel(const char* path, const char* texturePath);
void drawModel(const Model& model, const glm::mat4& view, const glm::mat4& projection);
GLuint loadTexture(const char* path);
