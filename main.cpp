// Enchanted Forest — Integrated 2D/3D demo
//
// This application demonstrates:
// - Basic OpenGL lines (2D overlay grid, path/circle outlines)
// - Bresenham's line algorithm (layout path generation on a discrete design grid)
// - Midpoint circle algorithm (annulus sampling for ring mesh and 2D fountain ring)
// - 3D model with texture mapping (OBJ fountain with fountain.png)
//
// Design overview
// - A discrete design grid (designGridW × designGridH) maps to the world XZ plane [-10,10]^2.
// - User paths are generated via Bresenham from random edge cells to the fountain cell.
// - A ring (annulus) is built around the fountain using circle sampling; path.png tiles 1:1
//   to each design-grid cell in world space.
// - Star hedge wedges (triangular prisms) form forbidden zones for paths/trees.
// - 2D view uses basic OpenGL line primitives and filled quads to visualize the grid,
//   paths, annulus, and trees. 3D view renders the ground, paths, OBJ fountain, hedges,
//   and trees with textures and fog/lighting.
//
// Controls (key highlights)
// - V: Toggle 2D/3D views (window title shows current view)
// - P: Cycle stylized path mesh (visual only). Accurate user paths always render when available.
// - [/]: Adjust fountain pixel radius (affects ring and overlays)
// - T/M: Cycle ground textures (grass/moss/purple)
// - I/O: Tree scale +/-   |  J: Trees yaw-left
// - K/L: Fountain scale +/-| U: Fountain yaw-right (yaw-only)
// - Mouse L: Plant a tree at cursor if not forbidden
// - R: Reset camera and transforms | ESC: Exit
//
// Notes on constraints
// - Paths and trees are excluded inside the full annulus (fountain to outer hedges) and
//   inside wedge footprints (triangles) for consistent design.
// - Tree auto-placement enforces minimum spacing and adapts (relaxes spacing) to reach
//   higher requested counts while honoring constraints.
// - Collision guard prevents fountain from scaling into hedges and pushes trees outward
//   if they intrude the hedges' outer disk.

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <algorithm> // for std::sort
#include <cmath>      // for std::llround, std::atan2
#include "model.h"
#include "shader_utils.h"
#include "cube_utils.h"
#include "bresenham.h"
#include "circle.h"
#include <unordered_map>
#include <unordered_set>

// ----------------- Globals -----------------
enum TreeSize { Small=0, Medium=1, Tall=2 };
struct TreeInst { glm::vec2 pos; TreeSize size; };
std::vector<TreeInst> treeInstances;
struct Glade { int gx; int gy; int radius; }; // design grid coordinates
struct LayoutPath { glm::ivec2 a; glm::ivec2 b; bool clear; };
std::vector<Glade> glades;
std::vector<LayoutPath> layoutPaths;
bool layoutGenerated = false;
glm::vec3 cameraPos   = glm::vec3(0.0f, 2.0f, 10.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);
float yawDeg = -90.0f; // facing -Z initially
float pitchDeg = 0.0f; // level pitch

Model treeModel;
Model fountainModel; // OBJ-based fountain
bool useProceduralFountain = false; // fallback if OBJ not found
// Procedural fountain replaces OBJ fountain
// Ground is procedural (quad), not a Model

GLuint shaderProgram;
GLuint fireflyVAO;
GLuint groundVAO = 0, groundVBO = 0, groundEBO = 0;
GLuint groundTextures[3] = {0,0,0};
int currentGroundTex = 0; // 0: grass, 1: moss, 2: purple
GLuint pathTexture = 0;
GLuint pathVAO = 0, pathVBO = 0, pathEBO = 0;
// Accurate layout-based path mesh (built from Bresenham grid cells)
GLuint layoutPathVAO = 0, layoutPathVBO = 0, layoutPathEBO = 0;
GLsizei layoutPathIndexCount = 0;
GLuint treeSpriteTex = 0, fountainSpriteTex = 0;
GLuint trunkTexture = 0, leavesTexture = 0;
float groundRepeat = 4.0f; // UV tiling factor for ground texture
// Dynamic path width (half extent from center line in world units)
float pathHalfWidth = 0.3f;
// Circular walkway ring geometry around fountain
GLuint ringVAO = 0, ringVBO = 0, ringEBO = 0;
GLsizei ringIndexCount = 0;
// Fountain scale used for procedural fountain and ring radius
float fountainScale = 0.35f;

// Procedural tree geometry (trunk cylinder + foliage cone)
GLuint trunkVAO=0, trunkVBO=0, trunkEBO=0; GLsizei trunkIndexCount=0;
GLuint coneVAO=0, coneVBO=0, coneEBO=0; GLsizei coneIndexCount=0;
// Global tree scale factor (applies to all 3D trees)
float treeScaleFactor = 2.0f;
// Separate transform controls for fountain and trees
float fountainGlobalScale = 1.0f;
float fountainYawDeg = 0.0f; // yaw-only
float treeGlobalScale = 1.2f; // default trees scaled to 1.2x
float treeYawDeg = 0.0f;     // yaw-only
// Hedge scale follows fountain (uniform XYZ)
// Hedge wedges are kept slightly smaller (0.8) relative to fountain global scale
float hedgeGlobalScale = 0.8f;


// Star hedge wedges (triangular prisms) parameters and geometry
struct Tri { glm::vec2 a, b, c; };
std::vector<Tri> hedgeWedgeTris; // world-space triangles for 2D occupancy
int hedgeInnerCount = 8, hedgeOuterCount = 16;
float wedgeRInner1 = 1.0f, wedgeROuter1 = 2.0f, wedgeHalfAng1 = glm::radians(12.0f);
float wedgeRInner2 = 2.6f, wedgeROuter2 = 3.8f, wedgeHalfAng2 = glm::radians(8.0f);
float hedgeHeight = 0.4f;
// Wedge templates (built after GL init)
GLuint wedgeVAO1=0, wedgeVBO1=0, wedgeEBO1=0; GLsizei wedgeIdx1=0;
GLuint wedgeVAO2=0, wedgeVBO2=0, wedgeEBO2=0; GLsizei wedgeIdx2=0;

struct Firefly {
    glm::vec3 position;
    float phase;
    float driftPhaseX;
    float driftPhaseZ;
    float blinkPhase;
    float blinkSpeed;
};
std::vector<Firefly> fireflies;
// Maintain per-tree radial margin to the outer circular hedge boundary so distance stays constant across scaling
static std::vector<float> treeOuterMargin; // margin = r - (wedgeROuter2 * hedgeGlobalScale) at time of placement
// Maintain per-tree constant radial gap to fountain footprint so tree-fountain distance stays stable across fountain scaling
static std::vector<float> treeFountainGap; // gap = r - (fountainScale * fountainGlobalScale * 1.1f) at placement

// ----------------- 2D Blueprint State -----------------
enum ViewMode { VIEW_2D, VIEW_3D };
ViewMode currentView = VIEW_3D;
bool showBlueprint = true;
int pathStyle = 0; // 0: straight, 1: polyline curve illusion, 2: branching
int fountainRadius = 60; // pixels in overlay space
int autoTreeCount = 5;
int designGridW = 50, designGridH = 50;
// Toggle between stylized demo paths and accurate layout reproduction
// Accurate layout-based path rendering is always enabled

// Screen size (keep in sync with window creation)
const int SCR_WIDTH = 800;
const int SCR_HEIGHT = 600;

// Simple key edge detector
static bool isKeyPressedOnce(GLFWwindow* w, int key) {
    static std::unordered_map<int,int> last;
    int state = glfwGetKey(w, key);
    int prev = last[key];
    last[key] = state;
    return state == GLFW_PRESS && prev != GLFW_PRESS;
}

// Debug flash indicator
float debugFlash = 0.0f;
glm::vec3 debugColor(1.0f, 1.0f, 1.0f);
static void debugFlashPing(const glm::vec3& c) { debugColor = c; debugFlash = 0.25f; }

// Forbid planting inside wedges' outer circle or inside wedge triangle footprints
static bool isForbiddenAtWorld(float wx, float wz) {
    // Returns true if a world position lies in a forbidden region:
    // - inside the hedges' outer disk
    // - inside any star hedge wedge triangle footprint
    // Outer circle check against current wedges' outer radius
    float r2 = wx*wx + wz*wz;
    float outerR = wedgeROuter2 * hedgeGlobalScale;
    if (outerR > 0.0f && r2 <= outerR*outerR) return true;
    // Triangle footprint check
    auto pointInTri2 = [](const glm::vec2& p, const Tri& t){
        auto sign=[&](const glm::vec2& p1,const glm::vec2& p2,const glm::vec2& p3){ return (p1.x - p3.x)*(p2.y - p3.y) - (p2.x - p3.x)*(p1.y - p3.y); };
        float d1 = sign(p, t.a, t.b), d2 = sign(p, t.b, t.c), d3 = sign(p, t.c, t.a);
        bool hasNeg = (d1<0) || (d2<0) || (d3<0);
        bool hasPos = (d1>0) || (d2>0) || (d3>0);
        return !(hasNeg && hasPos);
    };
    glm::vec2 p(wx, wz);
    for (auto &tri : hedgeWedgeTris) { if (pointInTri2(p, tri)) return true; }
    return false;
}

// ----------------- Helper Functions -----------------
void placeTree(float x, float y, TreeSize sz = Medium) {
    treeInstances.push_back(TreeInst{glm::vec2(x, y), sz});
}

void mouseCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        float worldX = (mx / (float)SCR_WIDTH) * 20.0f - 10.0f;
        float worldY = 10.0f - (my / (float)SCR_HEIGHT) * 20.0f;
        if (isForbiddenAtWorld(worldX, worldY)) {
            debugFlashPing(glm::vec3(1.0f, 0.3f, 0.3f)); // red flash for invalid placement
            return;
        }
        placeTree(worldX, worldY, Medium);
    }
}

void initFireflies(int count) {
    fireflyVAO = createCubeVAO();
    fireflies.clear();
    for (int i = 0; i < count; i++) {
        Firefly f;
        f.position = glm::vec3((rand()%200-100)/10.0f, (rand()%50)/10.0f + 1.0f, (rand()%200-100)/10.0f);
        f.phase = static_cast<float>(rand()%100)/100.0f;
        f.driftPhaseX = static_cast<float>(rand()%100)/100.0f * 6.2831f;
        f.driftPhaseZ = static_cast<float>(rand()%100)/100.0f * 6.2831f;
        f.blinkPhase = static_cast<float>(rand()%100)/100.0f;
        f.blinkSpeed = 1.0f + static_cast<float>(rand()%100)/100.0f;
        fireflies.push_back(f);
    }
}

void createGroundPlane() {
    // Simple 2x2 quad centered at origin, scaled later via model matrix if needed
    if (groundVAO) return;
    float vertices[] = {
        // positions           // normals     // uvs (repeat factor applied)
        -20.0f, 0.0f, -20.0f,  0,1,0,        0.0f,          0.0f,
         20.0f, 0.0f, -20.0f,  0,1,0,        groundRepeat,  0.0f,
         20.0f, 0.0f,  20.0f,  0,1,0,        groundRepeat,  groundRepeat,
        -20.0f, 0.0f,  20.0f,  0,1,0,        0.0f,          groundRepeat
    };
    unsigned int indices[] = { 0,1,2, 2,3,0 };

    glGenVertexArrays(1, &groundVAO);
    glGenBuffers(1, &groundVBO);
    glGenBuffers(1, &groundEBO);

    glBindVertexArray(groundVAO);
    glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, groundEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

// Update ground UVs when tiling factor changes
void updateGroundUVRepeat() {
    if (!groundVAO) return;
    glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    float vertices[] = {
        -20.0f, 0.0f, -20.0f,  0,1,0,        0.0f,          0.0f,
         20.0f, 0.0f, -20.0f,  0,1,0,        groundRepeat,  0.0f,
         20.0f, 0.0f,  20.0f,  0,1,0,        groundRepeat,  groundRepeat,
        -20.0f, 0.0f,  20.0f,  0,1,0,        0.0f,          groundRepeat
    };
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Create a simple cylinder along Y axis: height 1, radius r
static void createCylinder(float r, int segments) {
    // Builds a unit-height Y-aligned cylinder mesh (two triangle strips stitched into quads)
    // r: base radius in local space; segments: angular tessellation
    if (trunkVAO) return;
    std::vector<float> verts; std::vector<unsigned int> idx;
    for (int i=0;i<=segments;i++) {
        float t = (float)i/segments; float ang = t * 6.2831853f;
        float x = r * cosf(ang), z = r * sinf(ang);
        glm::vec3 n = glm::normalize(glm::vec3(x,0.0f,z));
        // bottom
        verts.insert(verts.end(), {x,0.0f,z, n.x,n.y,n.z, t,0.0f});
        // top
        verts.insert(verts.end(), {x,1.0f,z, n.x,n.y,n.z, t,1.0f});
    }
    for (int i=0;i<segments;i++) {
        unsigned int b0=i*2, t0=b0+1, b1=(i+1)*2, t1=b1+1;
        idx.insert(idx.end(), {b0,t0,t1, b0,t1,b1});
    }
    glGenVertexArrays(1,&trunkVAO); glGenBuffers(1,&trunkVBO); glGenBuffers(1,&trunkEBO);
    glBindVertexArray(trunkVAO);
    glBindBuffer(GL_ARRAY_BUFFER, trunkVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, trunkEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    trunkIndexCount = (GLsizei)idx.size();
}

// Create a cone along Y axis: base at 0 radius r, apex at 1
static void createCone(float r, int segments) {
    // Builds a Y-aligned cone mesh with base at y=0 and apex at y=1
    if (coneVAO) return;
    std::vector<float> verts; std::vector<unsigned int> idx;
    for (int i=0;i<=segments;i++) {
        float t = (float)i/segments; float ang = t * 6.2831853f;
        float x = r * cosf(ang), z = r * sinf(ang);
        glm::vec3 n = glm::normalize(glm::vec3(x, r, z));
        verts.insert(verts.end(), {x,0.0f,z, n.x,n.y,n.z, t,0.0f});
    }
    unsigned int apex = (unsigned int)(verts.size()/8);
    verts.insert(verts.end(), {0.0f,1.0f,0.0f, 0.0f,1.0f,0.0f, 0.5f,1.0f});
    for (int i=0;i<segments;i++) { unsigned int b0=i, b1=i+1; idx.insert(idx.end(), {b0,apex,b1}); }
    glGenVertexArrays(1,&coneVAO); glGenBuffers(1,&coneVBO); glGenBuffers(1,&coneEBO);
    glBindVertexArray(coneVAO);
    glBindBuffer(GL_ARRAY_BUFFER, coneVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, coneEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    coneIndexCount = (GLsizei)idx.size();
}

// Draw procedural fountain at world origin using cylinders and cones
static void drawProceduralFountain(GLuint shader, const glm::mat4& view, const glm::mat4& projection) {
    // Fallback fountain composed of cylinders/cone to demonstrate textured/solid rendering
    glUseProgram(shader);
    // Common stone color
    auto setColor = [&](float r,float g,float b){ glUniform3f(glGetUniformLocation(shader, "objectColor"), r,g,b); glUniform1i(glGetUniformLocation(shader, "solidMode"), 1); };
    auto unsetColor = [&](){ glUniform1i(glGetUniformLocation(shader, "solidMode"), 0); };

    float s = fountainScale;
    float baseY = 0.0f; // base sits on ground surface
    // Root transform from global controls
    glm::mat4 Root(1.0f);
    // Yaw-only rotation & scale for fountain
    Root = glm::rotate(Root, glm::radians(fountainYawDeg),   glm::vec3(0,1,0));
    Root = glm::scale(Root, glm::vec3(fountainGlobalScale));

    // Base plinth (stone cylinder)
    {
        glm::mat4 M(1.0f);
        M = glm::translate(M, glm::vec3(0.0f, baseY, 0.0f));
        // trunkVAO cylinder has unit height; scale Y to height
        float h = 0.30f * s; float r = 0.60f * s; float baseR = 0.08f; // created cylinder base radius
        M = glm::scale(M, glm::vec3(r/baseR, h/1.0f, r/baseR));
        M = Root * M;
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, &M[0][0]);
        setColor(0.78f, 0.78f, 0.82f);
        glBindVertexArray(trunkVAO);
        glDrawElements(GL_TRIANGLES, trunkIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        unsetColor();
    }

    // Pedestal column (stone cylinder)
    float colH = 0.60f * s; float colR = 0.18f * s;
    {
        glm::mat4 M(1.0f);
        M = glm::translate(M, glm::vec3(0.0f, baseY + 0.30f * s, 0.0f));
        M = glm::scale(M, glm::vec3(colR/0.08f, colH/1.0f, colR/0.08f));
        M = Root * M;
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, &M[0][0]);
        setColor(0.82f, 0.82f, 0.86f);
        glBindVertexArray(trunkVAO);
        glDrawElements(GL_TRIANGLES, trunkIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        unsetColor();
    }

    // Basin rim (wide shallow cylinder)
    float rimH = 0.12f * s; float rimR = 0.55f * s;
    float basinY = baseY + 0.30f*s + colH;
    {
        glm::mat4 M(1.0f);
        M = glm::translate(M, glm::vec3(0.0f, basinY, 0.0f));
        M = glm::scale(M, glm::vec3(rimR/0.08f, rimH/1.0f, rimR/0.08f));
        M = Root * M;
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, &M[0][0]);
        setColor(0.80f, 0.80f, 0.84f);
        glBindVertexArray(trunkVAO);
        glDrawElements(GL_TRIANGLES, trunkIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        unsetColor();
    }

    // Water disc (very shallow cylinder)
    float waterR = 0.45f * s; float waterH = 0.02f * s;
    {
        glm::mat4 M(1.0f);
        M = glm::translate(M, glm::vec3(0.0f, basinY + rimH*0.4f, 0.0f));
        M = glm::scale(M, glm::vec3(waterR/0.08f, waterH/1.0f, waterR/0.08f));
        M = Root * M;
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, &M[0][0]);
        setColor(0.55f, 0.70f, 0.95f);
        glBindVertexArray(trunkVAO);
        glDrawElements(GL_TRIANGLES, trunkIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        unsetColor();
    }

    // Top finial (small cone)
    float finH = 0.20f * s; float finR = 0.12f * s;
    {
        glm::mat4 M(1.0f);
        M = glm::translate(M, glm::vec3(0.0f, basinY + rimH + finH, 0.0f));
        M = glm::scale(M, glm::vec3(finR/0.20f, finH/1.0f, finR/0.20f));
        M = Root * M;
        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, &M[0][0]);
        setColor(0.82f, 0.82f, 0.86f);
        glBindVertexArray(coneVAO);
        glDrawElements(GL_TRIANGLES, coneIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        unsetColor();
    }
}

// Draw bushes as low green cones

// Build an isosceles triangular prism (wedge) template oriented along +X
static void createWedgeTemplate(float rInner, float rOuter, float halfAng, float height,
                                GLuint &vao, GLuint &vbo, GLuint &ebo, GLsizei &idxCount) {
    // Creates an isosceles triangular prism oriented along +X, used for star hedge wedges
    if (vao) return;
    // Triangle vertices in local XZ plane
    glm::vec3 A(rInner, 0.0f, 0.0f);
    glm::vec3 BL(rOuter * cosf(halfAng), 0.0f,  rOuter * sinf(halfAng));
    glm::vec3 BR(rOuter * cosf(halfAng), 0.0f, -rOuter * sinf(halfAng));

    auto faceNormal = [](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2){
        glm::vec3 u = p1 - p0, v = p2 - p0; return glm::normalize(glm::cross(u, v));
    };

    std::vector<float> verts; // pos(3), normal(3), uv(2)
    std::vector<unsigned int> idx;
    auto uvFor = [&](const glm::vec3& p){
        float u = (p.x / (2.0f*rOuter)) + 0.5f;
        float v = (height > 0.0f) ? (p.y / height) : 0.0f;
        return glm::vec2(u, v);
    };
    auto pushTri = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, const glm::vec3& n){
        size_t base = verts.size()/8;
        auto pushV=[&](const glm::vec3& p){ auto uv=uvFor(p); verts.insert(verts.end(), {p.x,p.y,p.z, n.x,n.y,n.z, uv.x, uv.y}); };
        pushV(p0); pushV(p1); pushV(p2);
        idx.push_back((unsigned int)base+0); idx.push_back((unsigned int)base+1); idx.push_back((unsigned int)base+2);
    };
    auto pushQuad = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3){
        glm::vec3 n = faceNormal(p0,p1,p2);
        size_t base = verts.size()/8;
        auto pushV=[&](const glm::vec3& p){ auto uv=uvFor(p); verts.insert(verts.end(), {p.x,p.y,p.z, n.x,n.y,n.z, uv.x, uv.y}); };
        pushV(p0); pushV(p1); pushV(p2); pushV(p3);
        idx.insert(idx.end(), {(unsigned int)base+0,(unsigned int)base+1,(unsigned int)base+2,
                               (unsigned int)base+2,(unsigned int)base+3,(unsigned int)base+0});
    };

    // Top/bottom triangles
    glm::vec3 At = A + glm::vec3(0,height,0), BLt = BL + glm::vec3(0,height,0), BRt = BR + glm::vec3(0,height,0);
    // Bottom (y=0) winding up
    pushTri(A, BR, BL, glm::vec3(0,1,0));
    // Top (y=height) winding down
    pushTri(At, BLt, BRt, glm::vec3(0,-1,0));

    // Sides (quads): A->BL, BL->BR, BR->A
    // A-BL side
    {
        pushQuad(A, At, BLt, BL);
    }
    // BL-BR side (base edge)
    {
        pushQuad(BL, BLt, BRt, BR);
    }
    // BR-A side
    {
        pushQuad(BR, BRt, At, A);
    }

    glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); glGenBuffers(1,&ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    idxCount = (GLsizei)idx.size();
}

static void buildHedgeMeshes() {
    // Build wedge templates for inner/outer rings based on stored wedge parameters
    createWedgeTemplate(wedgeRInner1, wedgeROuter1, wedgeHalfAng1, hedgeHeight, wedgeVAO1, wedgeVBO1, wedgeEBO1, wedgeIdx1);
    createWedgeTemplate(wedgeRInner2, wedgeROuter2, wedgeHalfAng2, hedgeHeight, wedgeVAO2, wedgeVBO2, wedgeEBO2, wedgeIdx2);
}

static void drawHedgeWedges(GLuint shader) {
    glUseProgram(shader);
    glUniform1i(glGetUniformLocation(shader, "solidMode"), 0);
    glActiveTexture(GL_TEXTURE0);
    // Use moss texture for hedges; reuse groundTextures[1]
    glBindTexture(GL_TEXTURE_2D, groundTextures[1]);
    glUniform1i(glGetUniformLocation(shader, "texture_diffuse1"), 0);
    // Apply uniform scale (follow fountain)
    auto setModel = [&](const glm::mat4& M){ glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, &M[0][0]); };
    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(hedgeGlobalScale, hedgeGlobalScale, hedgeGlobalScale));
    // Inner ring
    if (wedgeVAO1 && wedgeIdx1>0) {
        for (int i=0;i<hedgeInnerCount;i++) {
            float ang = (6.2831853f * i) / hedgeInnerCount;
            glm::mat4 M(1.0f);
            M = glm::rotate(M, ang, glm::vec3(0,1,0));
            M = S * M;
            setModel(M);
            glBindVertexArray(wedgeVAO1);
            glDrawElements(GL_TRIANGLES, wedgeIdx1, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }
    // Outer ring
    if (wedgeVAO2 && wedgeIdx2>0) {
        for (int i=0;i<hedgeOuterCount;i++) {
            float ang = (6.2831853f * i) / hedgeOuterCount + (3.14159f/hedgeOuterCount);
            glm::mat4 M(1.0f);
            M = glm::rotate(M, ang, glm::vec3(0,1,0));
            M = S * M;
            setModel(M);
            glBindVertexArray(wedgeVAO2);
            glDrawElements(GL_TRIANGLES, wedgeIdx2, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }
}

// Build textured annulus covering from fountain edge to outer hedge radius using pathTexture
void updateFountainRing(float /*fountainScaleUnused*/) {
    // Builds a textured annulus around the fountain using midpoint circle sampling.
    // UVs are world-aligned so each design-grid cell corresponds to exactly one texture tile.
    // Compute fountain world radius from 2D pixel radius
    int frGrid = std::max(2, (int)(fountainRadius / (std::min(SCR_WIDTH,SCR_HEIGHT)/(float)std::max(designGridW,designGridH))));
    float cellWorld = 20.0f / (float)designGridW;
    float innerR = frGrid * cellWorld; // from 2D fountain radius
    // Ensure inner radius also respects 3D fountain footprint derived from fountainScale
    innerR = std::max(innerR, fountainScale * 1.1f);
    // Outer radius: up to the OUTER edge of the (scaled) outer hedge ring, minus a small gap to avoid z-fighting
    float outerR = std::max(innerR + 0.05f, wedgeROuter2 * hedgeGlobalScale - 0.02f);

    int rPix = (int)std::round(outerR * 40.0f); // sampling density; higher factor gives smoother ring
    if (rPix < 16) rPix = 16;
    std::vector<glm::ivec2> raw;
    int x=0, y=rPix; int d=1-rPix;
    while (x <= y) {
        raw.push_back({x,y}); raw.push_back({-x,y}); raw.push_back({x,-y}); raw.push_back({-x,-y});
        raw.push_back({y,x}); raw.push_back({-y,x}); raw.push_back({y,-x}); raw.push_back({-y,-x});
        if (d < 0) d += 2*x + 3; else { d += 2*(x - y) + 5; y--; }
        x++;
    }

    struct AngPt { float ang; glm::vec2 dir; }; std::vector<AngPt> ordered; ordered.reserve(raw.size());
    std::unordered_set<long long> seen;

    for (auto &p : raw) {
        if (p.x==0 && p.y==0) continue;
        float ang = std::atan2((float)p.y, (float)p.x);
        long long key = (long long)std::llround(ang * 100000.0);
        if (seen.insert(key).second) {
            glm::vec2 dir = glm::normalize(glm::vec2((float)p.x, (float)p.y));
            ordered.push_back({ang, dir});
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const AngPt&a,const AngPt&b){return a.ang < b.ang;});
    if (ordered.size() < 24) return; // ensure adequate smoothness

    std::vector<float> verts; // pos(3), normal(3), uv(2)
    std::vector<unsigned int> indices;
    // World-space UVs: one texture tile per design-grid cell square
    float invCell = 1.0f / (20.0f / (float)designGridW);
    auto worldUV = [&](const glm::vec3& p){
        // Shift to [0,20] range then scale by invCell so each cell = 1 UV unit
        float u = (p.x + 10.0f) * invCell;
        float v = (p.z + 10.0f) * invCell;
        return glm::vec2(u, v);
    };
    auto pushV = [&](glm::vec3 pos, glm::vec2 uv){
        verts.push_back(pos.x); verts.push_back(pos.y); verts.push_back(pos.z);
        verts.push_back(0.0f); verts.push_back(1.0f); verts.push_back(0.0f);
        verts.push_back(uv.x); verts.push_back(uv.y);
    };
    for (size_t i=0;i<ordered.size();++i) {
        glm::vec2 dir = ordered[i].dir;
        glm::vec3 outerP(dir.x * outerR, 0.001f, dir.y * outerR);
        glm::vec3 innerP(dir.x * innerR, 0.001f, dir.y * innerR);
        // World-aligned tiling: exactly one path.png per grid cell square
        pushV(outerP, worldUV(outerP));
        pushV(innerP, worldUV(innerP));
    }
    unsigned int stride = 2;
    for (size_t i=0;i<ordered.size();++i) {
        size_t ni = (i+1)%ordered.size();
        unsigned int o0 = (unsigned int)i*stride; unsigned int i0 = o0+1;
        unsigned int o1 = (unsigned int)ni*stride; unsigned int i1 = o1+1;
        indices.push_back(o0); indices.push_back(i0); indices.push_back(i1);
        indices.push_back(o0); indices.push_back(i1); indices.push_back(o1);
    }

    if (!ringVAO) { glGenVertexArrays(1,&ringVAO); glGenBuffers(1,&ringVBO); glGenBuffers(1,&ringEBO); }
    glBindVertexArray(ringVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ringVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ringEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size()*sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    ringIndexCount = (GLsizei)indices.size();
}

// Build a simple ground-level textured path mesh based on current pathStyle
void updatePathMesh(int style) {
    if (!pathVAO) {
        glGenVertexArrays(1, &pathVAO);
        glGenBuffers(1, &pathVBO);
        glGenBuffers(1, &pathEBO);
    }
    std::vector<float> verts; // pos(3), normal(3), uv(2)
    std::vector<unsigned int> idx;

    auto addQuad = [&](glm::vec3 a, glm::vec3 b, float halfW, float uvLen){
        // Create a ribbon segment from a to b with width 2*halfW around ground y=0
        // Lift slightly above ground to avoid z-fighting
        a.y += 0.002f; b.y += 0.002f;
        glm::vec3 dir = glm::normalize(b - a);
        glm::vec3 right = glm::normalize(glm::vec3(dir.z, 0.0f, -dir.x));
        glm::vec3 n = glm::vec3(0,1,0);
        glm::vec3 p0 = a + right * halfW;
        glm::vec3 p1 = a - right * halfW;
        glm::vec3 p2 = b - right * halfW;
        glm::vec3 p3 = b + right * halfW;

        float base = (float)verts.size()/8.0f;
        auto pushV = [&](glm::vec3 p, float u, float v){
            verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
            verts.push_back(n.x); verts.push_back(n.y); verts.push_back(n.z);
            verts.push_back(u); verts.push_back(v);
        };
        pushV(p0, 0.0f, 0.0f);
        pushV(p1, 1.0f, 0.0f);
        pushV(p2, 1.0f, uvLen);
        pushV(p3, 0.0f, uvLen);
        unsigned int i0 = (unsigned int)base+0, i1=(unsigned int)base+1, i2=(unsigned int)base+2, i3=(unsigned int)base+3;
        idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
        idx.push_back(i2); idx.push_back(i3); idx.push_back(i0);
    };

    if (style == 0) {
        // Straight across X at z=0 (ground at y=0)
        addQuad(glm::vec3(-10.0f, 0.0f, 0.0f), glm::vec3(10.0f, 0.0f, 0.0f), pathHalfWidth, 8.0f);
    } else if (style == 1) {
        // Polyline zigzag
        std::vector<glm::vec3> pts = {
            glm::vec3(-8.0f,0.0f,-2.0f), glm::vec3(-4.0f,0.0f, 2.0f), glm::vec3(0.0f,0.0f,-2.0f),
            glm::vec3(4.0f,0.0f, 2.0f), glm::vec3(8.0f,0.0f,-2.0f)
        };
        for (size_t i=1;i<pts.size();++i) addQuad(pts[i-1], pts[i], pathHalfWidth, 2.0f);
    } else if (style == 2) {
        // Branching from center (ground y=0)
        addQuad(glm::vec3(-10.0f,0.0f,0.0f), glm::vec3(0.0f,0.0f,0.0f), pathHalfWidth, 4.0f);
        addQuad(glm::vec3(0.0f,0.0f,0.0f), glm::vec3(8.0f,0.0f, 6.0f), pathHalfWidth, 3.0f);
        addQuad(glm::vec3(0.0f,0.0f,0.0f), glm::vec3(8.0f,0.0f,-6.0f), pathHalfWidth, 3.0f);
    }

    glBindVertexArray(pathVAO);
    glBindBuffer(GL_ARRAY_BUFFER, pathVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pathEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Store count in EBO by binding later
}

// Convert design grid cell to world XZ (y fixed at 0)
static inline glm::vec3 gridToWorld(int gx, int gy) {
    float wx = (gx/(float)designGridW)*20.0f - 10.0f;
    float wz = (gy/(float)designGridH)*20.0f - 10.0f;
    return glm::vec3(wx, 0.0f, wz);
}

// Build mesh that exactly matches Bresenham layout paths (all unobstructed segments)
void updateAccuratePathMesh() {
    // Emits an accurate path ribbon mesh from Bresenham-generated discrete segments.
    // Segment midpoints are tested against forbidden regions to maintain constraints.
    if (!layoutGenerated || layoutPaths.empty()) return;
    if (!layoutPathVAO) { glGenVertexArrays(1,&layoutPathVAO); glGenBuffers(1,&layoutPathVBO); glGenBuffers(1,&layoutPathEBO); }
    std::vector<float> verts; // pos(3), normal(3), uv(2)
    std::vector<unsigned int> idx;
    // Compute fountain circle radius in world units to exclude segments inside it
    int frGrid = std::max(2, (int)(fountainRadius / (std::min(SCR_WIDTH,SCR_HEIGHT)/(float)std::max(designGridW,designGridH))));
    float cellWorld = 20.0f / (float)designGridW;
    float fWorldR = frGrid * cellWorld;
    // (Removed unused fWorldR2)
    float wedgeOuterR = std::max(wedgeROuter2 * hedgeGlobalScale, fWorldR); // scaled outer hedge radius
    float wedgeOuterR2 = wedgeOuterR * wedgeOuterR;
    auto pointInTri2 = [](const glm::vec2& p, const Tri& t){
        auto sign=[&](const glm::vec2& p1,const glm::vec2& p2,const glm::vec2& p3){ return (p1.x - p3.x)*(p2.y - p3.y) - (p2.x - p3.x)*(p1.y - p3.y); };
        float d1 = sign(p, t.a, t.b), d2 = sign(p, t.b, t.c), d3 = sign(p, t.c, t.a);
        bool hasNeg = (d1<0) || (d2<0) || (d3<0);
        bool hasPos = (d1>0) || (d2>0) || (d3>0);
        return !(hasNeg && hasPos);
    };
    auto segmentAllowed = [&](const glm::vec3& a, const glm::vec3& b){
        glm::vec3 m = 0.5f*(a+b);
        float d2 = m.x*m.x + m.z*m.z;
        // Exclude entire disk inside the (scaled) wedges' outer radius
        if (d2 <= wedgeOuterR2) return false;
        glm::vec2 pm(m.x, m.z);
        for (auto &tri : hedgeWedgeTris) {
            if (pointInTri2(pm, tri)) return false; // inside hedge wedge footprint
        }
        return true;
    };
    auto pushQuad = [&](const glm::vec3& a, const glm::vec3& b){
        glm::vec3 dir = b - a;
        float segLen = glm::length(dir);
        if (segLen < 1e-4f) return;
        dir /= segLen;
        glm::vec3 right = glm::normalize(glm::vec3(dir.z, 0.0f, -dir.x));
        glm::vec3 n(0,1,0);
        // Lift slightly above ground to avoid z-fighting
        glm::vec3 a2 = a; a2.y += 0.002f; glm::vec3 b2 = b; b2.y += 0.002f;
        // Radial outward offset near the hedge boundary to "move paths outward" with the circular area
        float band = 0.25f; // width of boundary band
        float rOuter = wedgeOuterR; // scaled outer radius
        auto applyRadialOffset = [&](glm::vec3& p){
            float r = sqrtf(p.x*p.x + p.z*p.z);
            // Apply outward offset just outside the hedge boundary band
            if (r >= rOuter && r < rOuter + band && r > 1e-4f) {
                float t = (r - rOuter) / band;              // 0..1 across outer band
                float offset = 0.25f * (0.6f + 0.4f * t);    // slightly stronger outward offset
                glm::vec3 dirRad = glm::normalize(glm::vec3(p.x, 0.0f, p.z));
                p += dirRad * offset;
            }
        };
        applyRadialOffset(a2);
        applyRadialOffset(b2);
        glm::vec3 p0 = a2 + right * pathHalfWidth;
        glm::vec3 p1 = a2 - right * pathHalfWidth;
        glm::vec3 p2 = b2 - right * pathHalfWidth;
        glm::vec3 p3 = b2 + right * pathHalfWidth;
        unsigned int base = (unsigned int)(verts.size()/8);
        auto addV=[&](const glm::vec3& p,float u,float v){
            verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
            verts.push_back(n.x); verts.push_back(n.y); verts.push_back(n.z);
            verts.push_back(u); verts.push_back(v);
        };
        // UVs: map exactly one texture tile per grid step (0..1 over length and width)
        addV(p0, 0.0f, 0.0f);
        addV(p1, 1.0f, 0.0f);
        addV(p2, 1.0f, 1.0f);
        addV(p3, 0.0f, 1.0f);
        idx.push_back(base+0); idx.push_back(base+1); idx.push_back(base+2);
        idx.push_back(base+2); idx.push_back(base+3); idx.push_back(base+0);
    };
    // Recreate Bresenham per path and emit quads between successive cells
    for (auto &lp : layoutPaths) {
        // Only draw clear paths
        if (!lp.clear) continue;
        int x0=lp.a.x, y0=lp.a.y, x1=lp.b.x, y1=lp.b.y;
        int dx=abs(x1-x0), dy=abs(y1-y0); int sx = x0<x1?1:-1; int sy=y0<y1?1:-1; int err=dx-dy;
        int px=x0, py=y0;
        int prevX=px, prevY=py;
        while (true) {
            if (px==x1 && py==y1) {
                // last segment
                glm::vec3 a = gridToWorld(prevX, prevY);
                glm::vec3 b = gridToWorld(px, py);
                if (segmentAllowed(a,b)) pushQuad(a,b);
                break;
            }
            int e2=2*err; if (e2> -dy){ err -= dy; px += sx; } if (e2 < dx){ err += dx; py += sy; }
            glm::vec3 a = gridToWorld(prevX, prevY);
            glm::vec3 b = gridToWorld(px, py);
            if (segmentAllowed(a,b)) pushQuad(a,b);
            prevX=px; prevY=py;
        }
    }
    glBindVertexArray(layoutPathVAO);
    glBindBuffer(GL_ARRAY_BUFFER, layoutPathVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, layoutPathEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    layoutPathIndexCount = (GLsizei)idx.size();
}

// Simple NDC triangle VAO (for pipeline sanity check)
// (Removed NDC triangle debug)

// ----------------- Draw Helpers -----------------
void setCommonUniforms(GLuint shader, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos) {
    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, &view[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, &projection[0][0]);
    glUniform3f(glGetUniformLocation(shader, "lightDir"), -0.5f, -1.0f, -0.3f);
    glUniform3f(glGetUniformLocation(shader, "viewPos"), camPos.x, camPos.y, camPos.z);
    // Slightly brighter lighting and thinner fog
    glUniform3f(glGetUniformLocation(shader, "lightColor"), 1.2f, 1.2f, 1.15f);
    glUniform3f(glGetUniformLocation(shader, "fogColor"), 0.1f, 0.15f, 0.2f);
    glUniform1f(glGetUniformLocation(shader, "fogDensity"), 0.015f);
    glUniform1i(glGetUniformLocation(shader, "solidMode"), 0);
}

// Generic model drawer
void drawObject(Model& model, const glm::vec3& position, GLuint shader, const glm::mat4& view, const glm::mat4& projection) {
    model.position = position;
    drawModel(model, view, projection);
}

// Draw fireflies with additive blending
void drawFireflies(GLuint shader, const glm::mat4& view, const glm::mat4& projection, float time) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glUseProgram(shader);
    glBindVertexArray(fireflyVAO);

    for (auto& f : fireflies) {
        glm::vec3 pos = f.position;
        pos.y += sin(time + f.phase) * 0.3f;
        pos.x += sin(time + f.driftPhaseX) * 0.1f;
        pos.z += cos(time + f.driftPhaseZ) * 0.1f;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), pos);
        model = glm::scale(model, glm::vec3(0.05f));

        glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, &model[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, &projection[0][0]);

        float intensity = 0.5f + 0.5f * sin(time * f.blinkSpeed + f.blinkPhase * 6.2831f);
        float distance = glm::length(cameraPos - pos);
        float fade = glm::clamp(1.0f - distance / 20.0f, 0.0f, 1.0f);
        intensity *= fade;

        glUniform3f(glGetUniformLocation(shader, "objectColor"), 1.0f * intensity, 1.0f * intensity, 0.5f * intensity);
        glUniform1i(glGetUniformLocation(shader, "solidMode"), 1);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        glUniform1i(glGetUniformLocation(shader, "solidMode"), 0);
    }

    glBindVertexArray(0);
    glDisable(GL_BLEND); // disable after firefly pass so opaque models aren't blended
}

// (Removed debug cube rendering)

// ----------------- 2D Overlay (Blueprint) -----------------
static void beginOrtho2D(int width, int height) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, 0, height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

static void endOrtho2D() {
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

// Convert world XZ to screen XY for overlay
static inline glm::ivec2 worldToScreen(const glm::vec2& w) {
    int sx = (int)((w.x + 10.0f) / 20.0f * SCR_WIDTH);
    int sy = (int)((10.0f - w.y) / 20.0f * SCR_HEIGHT);
    return glm::ivec2(sx, sy);
}

void drawBlueprintOverlay() {
    // Pixel grid-based overlay for 2D view
    if (currentView != VIEW_2D) return;
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
    beginOrtho2D(SCR_WIDTH, SCR_HEIGHT);

    // Grid parameters: auto-scale so the whole map fits with margins and reserved legend space
    int margin = 16;
    int reservedBottom = 28; // leave room for legend and flash bar
    int maxCellW = std::max(1, (SCR_WIDTH  - 2*margin) / std::max(1, designGridW));
    int maxCellH = std::max(1, (SCR_HEIGHT - 2*margin - reservedBottom) / std::max(1, designGridH));
    int cellSize = std::max(4, std::min(maxCellW, maxCellH));
    int gridWpx = designGridW * cellSize;
    int gridHpx = designGridH * cellSize;
    // Center the grid horizontally and vertically (above reservedBottom)
    int originX = std::max(margin, (SCR_WIDTH - gridWpx)/2);
    int originY = std::max(margin, (SCR_HEIGHT - reservedBottom - gridHpx)/2);
    auto cellToScreen = [&](int gx, int gy){ return glm::ivec2(originX + gx*cellSize, originY + gy*cellSize); };

    // Cyan border
    glColor3f(0.0f, 0.8f, 0.85f);
    glBegin(GL_LINE_LOOP);
        glVertex2i(originX, originY);
        glVertex2i(originX + gridWpx, originY);
        glVertex2i(originX + gridWpx, originY + gridHpx);
        glVertex2i(originX, originY + gridHpx);
    glEnd();

    // Grid lines
    glColor3f(0.18f, 0.18f, 0.20f);
    for (int gx=0; gx<=designGridW; ++gx) {
        int x = originX + gx*cellSize;
        glBegin(GL_LINES); glVertex2i(x, originY); glVertex2i(x, originY + gridHpx); glEnd();
    }
    for (int gy=0; gy<=designGridH; ++gy) {
        int y = originY + gy*cellSize;
        glBegin(GL_LINES); glVertex2i(originX, y); glVertex2i(originX + gridWpx, y); glEnd();
    }

    // Occupancy maps: path, fountain core, annulus (fountain edge -> wedges), tree
    std::vector<std::vector<unsigned char>> occ(designGridW, std::vector<unsigned char>(designGridH, 0));
    // Fountain core and ring (compute first so we can prevent path marks inside)
    glm::ivec2 fCenter(designGridW/2, designGridH/2);
    // Map pixel fountainRadius to grid cells consistently with 3D logic
    int fountainGridR = std::max(2, (int)(fountainRadius / (std::min(SCR_WIDTH,SCR_HEIGHT)/(float)std::max(designGridW,designGridH))));
    // Use current scaled hedge outer radius to derive grid-space wedge boundary
    float cellWorld = 20.0f / (float)designGridW;
    float wedgeOuterGridR = (wedgeROuter2 * hedgeGlobalScale) / cellWorld;
    for (int gx=0; gx<designGridW; ++gx) {
        for (int gy=0; gy<designGridH; ++gy) {
            int dx = gx - fCenter.x, dy = gy - fCenter.y; int d2 = dx*dx + dy*dy;
            if (d2 <= (fountainGridR-2)*(fountainGridR-2)) {
                occ[gx][gy] = 2; // core fountain (bluish white)
            } else if (d2 <= fountainGridR*fountainGridR) {
                if (occ[gx][gy]==0) occ[gx][gy] = 3; // inner ring (thin)
            }
        }
    }
    // Helper: point in triangle for hedge exclusion
    auto pointInTri2 = [](const glm::vec2& p, const Tri& t){
        auto sign=[&](const glm::vec2& p1,const glm::vec2& p2,const glm::vec2& p3){ return (p1.x - p3.x)*(p2.y - p3.y) - (p2.x - p3.x)*(p1.y - p3.y); };
        float d1 = sign(p, t.a, t.b), d2 = sign(p, t.b, t.c), d3 = sign(p, t.c, t.a);
        bool hasNeg = (d1<0) || (d2<0) || (d3<0);
        bool hasPos = (d1>0) || (d2>0) || (d3>0);
        return !(hasNeg && hasPos);
    };
    // Mark paths using Bresenham, but skip any cell inside the full annulus (fountain -> wedges)
    // or inside hedge wedge footprints
    if (layoutGenerated && !layoutPaths.empty()) {
        for (auto &lp : layoutPaths) {
            int x0=lp.a.x, y0=lp.a.y, x1=lp.b.x, y1=lp.b.y;
            int dx=abs(x1-x0), dy=abs(y1-y0); int sx = x0<x1?1:-1; int sy=y0<y1?1:-1; int err=dx-dy;
            while(true){
                if (x0>=0&&x0<designGridW&&y0>=0&&y0<designGridH) {
                    int ddx = x0 - fCenter.x, ddy = y0 - fCenter.y; int d2 = ddx*ddx + ddy*ddy;
                    bool inCircle = d2 <= (int)std::ceil(wedgeOuterGridR*wedgeOuterGridR); // exclude entire annulus up to wedges
                    bool inHedge = false;
                    if (!inCircle) {
                        glm::vec3 w = gridToWorld(x0, y0);
                        glm::vec2 p(w.x, w.z);
                        for (auto &tri : hedgeWedgeTris) { if (pointInTri2(p, tri)) { inHedge=true; break; } }
                    }
                    if (!inCircle && !inHedge && occ[x0][y0]==0) occ[x0][y0] = 1; // path only if empty and allowed
                }
                if (x0==x1 && y0==y1) break;
                int e2=2*err; if (e2> -dy){ err -= dy; x0 += sx; } if (e2 < dx){ err += dx; y0 += sy; }
            }
        }
    }
    // Trees: mark tree cells to avoid grass painting; markers drawn later with sizes
    for (auto &ti : treeInstances) {
        int gx = (int)glm::clamp(((ti.pos.x + 10.0f) / 20.0f) * designGridW + 0.5f, 0.0f, (float)designGridW - 1.0f);
        int gy = (int)glm::clamp(((ti.pos.y + 10.0f) / 20.0f) * designGridH + 0.5f, 0.0f, (float)designGridH - 1.0f);
        occ[gx][gy] = 4; // tree present
    }
    // Hedge wedges: mark cells inside any wedge triangle footprint (reuse pointInTri2 defined above)
    for (int gx=0; gx<designGridW; ++gx) {
        for (int gy=0; gy<designGridH; ++gy) {
            if (occ[gx][gy] != 0) continue; // keep higher-priority features
            glm::vec3 w = gridToWorld(gx, gy);
            glm::vec2 p(w.x, w.z);
            for (auto &tri : hedgeWedgeTris) {
                if (pointInTri2(p, tri)) { occ[gx][gy] = 5; break; }
            }
        }
    }

    // Annulus fill: mark remaining cells between fountain edge and outer wedge radius as yellow
    for (int gx=0; gx<designGridW; ++gx) {
        for (int gy=0; gy<designGridH; ++gy) {
            if (occ[gx][gy] != 0) continue; // don't override paths/trees/hedges/core
            int dx = gx - fCenter.x, dy = gy - fCenter.y; int d2 = dx*dx + dy*dy;
            if (d2 > fountainGridR*fountainGridR && d2 <= wedgeOuterGridR*wedgeOuterGridR) {
                occ[gx][gy] = 3; // annulus area (yellow)
            }
        }
    }

    // Paint cells: grass (light green) where empty, path brown, fountain bluish white, annulus yellow, trees dark green, bushes mid green
    auto fillCell = [&](int gx, int gy, float r, float g, float b, int pad){
        glm::ivec2 tl = cellToScreen(gx, gy);
        glColor3f(r,g,b);
        glBegin(GL_QUADS);
            glVertex2i(tl.x+pad, tl.y+pad);
            glVertex2i(tl.x+cellSize-pad, tl.y+pad);
            glVertex2i(tl.x+cellSize-pad, tl.y+cellSize-pad);
            glVertex2i(tl.x+pad, tl.y+cellSize-pad);
        glEnd();
    };
    for (int gx=0; gx<designGridW; ++gx) {
        for (int gy=0; gy<designGridH; ++gy) {
            switch(occ[gx][gy]) {
                case 0: // empty grass
                    fillCell(gx, gy, 0.60f, 0.85f, 0.60f, 3); // light green
                    break;
                case 1: // path
                    fillCell(gx, gy, 0.55f, 0.40f, 0.20f, 3); // brown
                    break;
                case 2: // fountain core
                    fillCell(gx, gy, 0.85f, 0.90f, 0.98f, 2); // bluish white
                    break;
                case 3: // fountain ring / annulus
                    fillCell(gx, gy, 0.95f, 0.92f, 0.35f, 2); // yellow
                    break;
                case 4: // tree cell (background keep dark to emphasize marker)
                    fillCell(gx, gy, 0.10f, 0.35f, 0.18f, 4); // darker base under marker
                    break;
                case 5: // bush
                    fillCell(gx, gy, 0.25f, 0.70f, 0.35f, 3); // mid green
                    break;
            }
        }
    }

    // Legend color chips
    // Place legend under grid if there is space; otherwise above the grid
    int proposedLegendY = originY + gridHpx + 8;
    int legendY = proposedLegendY;
    if (legendY + 12 > SCR_HEIGHT) legendY = std::max(8, originY - 18);
    int lx = originX;
    auto legendRect=[&](float r,float g,float b){
        glColor3f(r,g,b);
        glBegin(GL_QUADS);
            glVertex2i(lx, legendY);
            glVertex2i(lx+18, legendY);
            glVertex2i(lx+18, legendY+10);
            glVertex2i(lx, legendY+10);
        glEnd();
        lx += 26;
    };
    legendRect(0.10f,0.35f,0.18f); // Trees (dark green)
    legendRect(0.25f,0.70f,0.35f); // Bushes (mid green)
    legendRect(0.60f,0.85f,0.60f); // Grass (light green)
    legendRect(0.85f,0.90f,0.98f); // Fountain (bluish white)
    legendRect(0.55f,0.40f,0.20f); // Path (brown)
    legendRect(0.95f,0.92f,0.35f); // Fountain ring (yellow)

    // Debug flash
    if (debugFlash > 0.0f) {
        glColor3f(debugColor.r, debugColor.g, debugColor.b);
        glBegin(GL_QUADS);
            glVertex2i(SCR_WIDTH - 120, 40);
            glVertex2i(SCR_WIDTH - 10, 40);
            glVertex2i(SCR_WIDTH - 10, 10);
            glVertex2i(SCR_WIDTH - 120, 10);
        glEnd();
    }

    endOrtho2D();
    glEnable(GL_DEPTH_TEST);
}

// Draw 2D pixel glyphs for trees (fountain handled in overlay) in VIEW_2D
void drawPixelObjects2D() {
    if (currentView != VIEW_2D) return;
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
    beginOrtho2D(SCR_WIDTH, SCR_HEIGHT);

    auto drawFilledPixel = [](int px, int py, int size){
        glBegin(GL_QUADS);
            glVertex2i(px, py);
            glVertex2i(px + size, py);
            glVertex2i(px + size, py + size);
            glVertex2i(px, py + size);
        glEnd();
    };

    // Trees: draw markers at exact mapped screen positions to avoid overlapping in the same cell
    int margin = 16;
    int reservedBottom = 28;
    int maxCellW = std::max(1, (SCR_WIDTH  - 2*margin) / std::max(1, designGridW));
    int maxCellH = std::max(1, (SCR_HEIGHT - 2*margin - reservedBottom) / std::max(1, designGridH));
    int cellSize = std::max(4, std::min(maxCellW, maxCellH));
    int gridWpx = designGridW * cellSize;
    int gridHpx = designGridH * cellSize;
    int originX = std::max(margin, (SCR_WIDTH - gridWpx)/2);
    int originY = std::max(margin, (SCR_HEIGHT - reservedBottom - gridHpx)/2);
    auto worldToScreenOverlay = [&](float wx, float wz){
        float xNorm = ((wx + 10.0f) / 20.0f) * (float)designGridW;
        float yNorm = ((wz + 10.0f) / 20.0f) * (float)designGridH;
        int sx = originX + (int)std::round(xNorm * cellSize);
        int sy = originY + (int)std::round(yNorm * cellSize);
        return glm::ivec2(sx, sy);
    };

    for (auto &ti : treeInstances) {
        glm::ivec2 s = worldToScreenOverlay(ti.pos.x, ti.pos.y);
        // Size: 2x2 for small, 3x3 for medium, 4x4 for tall for better visibility
        int marker = (ti.size==Small?2:(ti.size==Medium?3:4));
        int x0 = s.x - marker/2;
        int y0 = s.y - marker/2;
        glColor3f(0.15f, 0.65f, 0.35f);
        drawFilledPixel(x0, y0, marker);
    }

    endOrtho2D();
    glEnable(GL_DEPTH_TEST);
}

// ----------------- Main -----------------
int main() {
    // --- Enchanted Forest Layout Generation Console ---
    {
    // Console bootstrap collects user preferences for counts/styles and logs the resulting layout.
    // The layout uses Bresenham for paths and world-based forbidden-zone checks for trees.
    // Announce core techniques used before forest generation
    std::cout << "[Info] Using Basic OpenGL lines for 2D overlay grid and path/circle outlines.\n";
    std::cout << "[Info] Using Bresenham's line algorithm to generate discrete layout paths.\n";
    std::cout << "[Info] Using Midpoint circle algorithm to sample and build the fountain annulus ring.\n";
    std::cout << "[Info] Rendering a 3D model with texture mapping (fountain OBJ + fountain.png).\n\n";
    // Student / Project identity
    std::cout << "Name        : Dilara Liyanage\n";
    std::cout << "Student ID  : IT23285606\n";
    std::cout << "Project     : Enchanted Forest\n";
    std::cout << "Project Idea: Interactive procedural 2D/3D forest showing algorithmic path generation, annulus tiling, constrained scaling, and dynamic tree distribution.\n"; // brief description
    std::cout << "----------------------------------------\n\n";
        std::cout << "=== ENCHANTED FOREST LAYOUT BOOTSTRAP ===\n";
        std::cout << "========================================\n\n";
        int smallCount = 5, mediumCount = 10, tallCount = 5;
        int pathCount = 5;
        std::string line;
        auto readRange = [&](const char* prompt,int& var,int minV,int maxV){
            std::cout << prompt << " (" << minV << "-" << maxV << ") [" << var << "]: ";
            if (std::getline(std::cin, line)) if(!line.empty()) { try { int v=std::stoi(line); if(v>=minV&&v<=maxV) var=v; } catch(...) {} }
        };
        readRange("Trees Small", smallCount, 0, 50);
        readRange("Trees Medium", mediumCount, 0, 50);
        readRange("Trees Tall", tallCount, 0, 50);
        // Glades removed: no input prompt
        readRange("Mystic Paths", pathCount, 1, 12);
        readRange("Fountain radius px", fountainRadius, 20, 200);
        readRange("Ground texture (0=grass,1=moss,2=purple)", currentGroundTex, 0, 2);
        readRange("Path style (0=straight,1=polyline,2=branching)", pathStyle, 0, 2);

        // Glades removed: no generation
        glades.clear();

        // Bresenham helper (integer grid): returns inclusive list of grid cells from A to B
        auto bresenham = [&](glm::ivec2 a, glm::ivec2 b){
            std::vector<glm::ivec2> pts; int x0=a.x, y0=a.y, x1=b.x, y1=b.y; int dx=abs(x1-x0), dy=abs(y1-y0);
            int sx = x0<x1?1:-1; int sy = y0<y1?1:-1; int err = dx-dy;
            while(true){ pts.push_back({x0,y0}); if(x0==x1 && y0==y1) break; int e2=2*err; if(e2> -dy){ err -= dy; x0 += sx; } if(e2 < dx){ err += dx; y0 += sy; } }
            return pts;
        };

        // Hub-style paths: connect random allowed forest cells back to the central fountain cell
        // Fountain is at design grid center
        layoutPaths.clear();
        glm::ivec2 fountainCell(designGridW/2, designGridH/2);
        std::vector<glm::ivec2> allPathCells; // to avoid tree placement on paths
        // Compute wedge outer radius in GRID units using same factors as hedges (ROuter2 = 3.6 * fWorldR)
        int frGrid_paths = std::max(2, (int)(fountainRadius / (std::min(SCR_WIDTH,SCR_HEIGHT)/(float)std::max(designGridW,designGridH))));
        float wedgeOuterGrid = frGrid_paths * 3.6f;
        auto outsideWedgeCircle = [&](const glm::ivec2& cell){
            int dx = cell.x - fountainCell.x; int dy = cell.y - fountainCell.y;
            return (dx*dx + dy*dy) > (int)std::ceil(wedgeOuterGrid*wedgeOuterGrid);
        };
        auto sampleStartOutside = [&](){
            // Try random sampling first
            for (int t=0;t<4000;t++) {
                glm::ivec2 cand(rand()%designGridW, rand()%designGridH);
                if (outsideWedgeCircle(cand)) return cand;
            }
            // Fallback: pick a random angle and place on boundary near the edge of grid
            float ang = ((rand()%1000)/1000.0f) * 6.2831853f;
            float r = std::max({fountainCell.x, designGridW-1 - fountainCell.x, fountainCell.y, designGridH-1 - fountainCell.y}) - 1.0f;
            glm::ivec2 cand(
                fountainCell.x + (int)std::round(r * cosf(ang)),
                fountainCell.y + (int)std::round(r * sinf(ang))
            );
            cand.x = std::max(0, std::min(designGridW-1, cand.x));
            cand.y = std::max(0, std::min(designGridH-1, cand.y));
            return cand;
        };
        for (int i=0;i<pathCount;i++) {
            glm::ivec2 a = sampleStartOutside();
            glm::ivec2 b = fountainCell; // all paths terminate at fountain
            auto pts = bresenham(a,b);
            layoutPaths.push_back({a,b,true});
            // record path cells for tree exclusion
            allPathCells.insert(allPathCells.end(), pts.begin(), pts.end());
        }

        // Hedge wedges: compute world-space triangle footprints before tree placement
        hedgeWedgeTris.clear();
        {
            int frGrid = std::max(2, (int)(fountainRadius / (std::min(SCR_WIDTH,SCR_HEIGHT)/(float)std::max(designGridW,designGridH))));
            float cellWorld = 20.0f / (float)designGridW;
            float fWorldR = frGrid * cellWorld;
            // Reduced radii for a tighter star pattern
            wedgeRInner1 = fWorldR * 1.4f; wedgeROuter1 = fWorldR * 2.4f; wedgeHalfAng1 = glm::radians(12.0f);
            wedgeRInner2 = fWorldR * 2.6f; wedgeROuter2 = fWorldR * 3.6f; wedgeHalfAng2 = glm::radians(8.0f);
            hedgeInnerCount = 8; hedgeOuterCount = 16;
            auto rot2 = [](const glm::vec2& p, float ang){ return glm::vec2(p.x*cosf(ang)-p.y*sinf(ang), p.x*sinf(ang)+p.y*cosf(ang)); };
            // base triangles oriented along +X
            auto makeTriLocal = [](float rIn, float rOut, float hAng){
                glm::vec2 A(rIn, 0.0f), BL(rOut*cosf(hAng),  rOut*sinf(hAng)), BR(rOut*cosf(hAng), -rOut*sinf(hAng));
                return Tri{A,BL,BR};
            };
            Tri t1 = makeTriLocal(wedgeRInner1, wedgeROuter1, wedgeHalfAng1);
            Tri t2 = makeTriLocal(wedgeRInner2, wedgeROuter2, wedgeHalfAng2);
            for (int i=0;i<hedgeInnerCount;i++) {
                float ang = (6.2831853f * i) / hedgeInnerCount;
                hedgeWedgeTris.push_back({ rot2(t1.a,ang), rot2(t1.b,ang), rot2(t1.c,ang) });
            }
            for (int i=0;i<hedgeOuterCount;i++) {
                float ang = (6.2831853f * i) / hedgeOuterCount + (3.14159f/hedgeOuterCount);
                hedgeWedgeTris.push_back({ rot2(t2.a,ang), rot2(t2.b,ang), rot2(t2.c,ang) });
            }
        }

        // Place trees uniformly around the allowed area (outside hedge disk and wedge footprints),
        // avoid paths, and enforce minimum spacing for even distribution. Adapt spacing if needed.
        treeInstances.clear();
        int targetTotal = smallCount + mediumCount + tallCount;
        // Fast lookup for path cells
        struct CellHash { size_t operator()(const glm::ivec2& c) const noexcept { return (size_t)((c.x & 0xFFFF) << 16) ^ (size_t)(c.y & 0xFFFF); } };
        struct CellEq { bool operator()(const glm::ivec2& a, const glm::ivec2& b) const noexcept { return a.x==b.x && a.y==b.y; } };
        std::unordered_set<glm::ivec2, CellHash, CellEq> pathCells;
        for (auto &pc : allPathCells) pathCells.insert(pc);
        auto worldToGrid = [&](float wx, float wz){
            int gx = (int)glm::clamp(((wx + 10.0f) / 20.0f) * designGridW + 0.5f, 0.0f, (float)designGridW - 1.0f);
            int gy = (int)glm::clamp(((wz + 10.0f) / 20.0f) * designGridH + 0.5f, 0.0f, (float)designGridH - 1.0f);
            return glm::ivec2(gx, gy);
        };
        auto forbiddenWorld = [&](float wx, float wz){ return isForbiddenAtWorld(wx, wz); };
        float minR = wedgeROuter2 + 0.20f; // stay outside hedges outer disk with small gap
        float minSpacingStart = 1.8f;      // keep spacing constant; +1 over previous
        float minSpacing = minSpacingStart;
        int sPlaced=0, mPlaced=0, tPlaced=0;
        for (int pass=0; pass<8 && (int)treeInstances.size() < targetTotal; ++pass) {
            int attempts = 0, maxAttempts = targetTotal * 600;
            while ((int)treeInstances.size() < targetTotal && attempts < maxAttempts) {
                attempts++;
                // Sample uniformly over world bounds; reject forbidden areas for truly scattered placement
                float wx = -10.0f + ((rand()%10000)/10000.0f) * 20.0f;
                float wz = -10.0f + ((rand()%10000)/10000.0f) * 20.0f;
                // Bounds clamp (world is [-10,10])
                if (wx < -10.0f || wx > 10.0f || wz < -10.0f || wz > 10.0f) continue;
                // Skip forbidden zones (outer disk and wedge triangles)
                if (forbiddenWorld(wx, wz)) continue;
                // Maintain minimum gap from hedges outer radius
                float r = sqrtf(wx*wx + wz*wz);
                if (r < minR) continue;
                // Skip path cells
                glm::ivec2 gc = worldToGrid(wx, wz);
                if (pathCells.find(gc) != pathCells.end()) continue;
                // Enforce minimum spacing from existing trees
                bool tooClose = false;
                for (auto &ti : treeInstances) {
                    float dx = ti.pos.x - wx, dz = ti.pos.y - wz;
                    if ((dx*dx + dz*dz) < (minSpacing * minSpacing)) { tooClose = true; break; }
                }
                if (tooClose) continue;
                // Size distribution
                TreeSize assign = Medium;
                if (sPlaced < smallCount) assign = Small;
                else if (mPlaced < mediumCount) assign = Medium;
                else assign = Tall;
                if (assign == Small) sPlaced++; else if (assign == Medium) mPlaced++; else tPlaced++;
                treeInstances.push_back(TreeInst{glm::vec2(wx, wz), assign});
            }
            // Keep spacing constant across passes (no relaxation)
            (void)pass; // spacing remains fixed
        }
        autoTreeCount = (int)treeInstances.size(); // prevent legacy autoplace from adding more
        // Initialize per-tree margin from current outer hedge radius so future scaling preserves gap
        treeOuterMargin.clear();
        treeFountainGap.clear();
        float currentOuter = wedgeROuter2 * hedgeGlobalScale;
        float currentFountainFoot = fountainScale * fountainGlobalScale * 1.1f;
        treeOuterMargin.reserve(treeInstances.size());
        treeFountainGap.reserve(treeInstances.size());
        for (auto &ti : treeInstances) {
            float r = glm::length(glm::vec2(ti.pos.x, ti.pos.y));
            treeOuterMargin.push_back(std::max(0.0f, r - currentOuter));
            treeFountainGap.push_back(std::max(0.0f, r - currentFountainFoot));
        }

        // Console Output
        // Glades removed (no glade generation in this design)
        std::cout << "[*] Weaving mystic paths to the central fountain...\n";
        int pi=1; for (auto &p: layoutPaths) {
            std::cout << "    Path "<<pi++<<": ("<<p.a.x<<","<<p.a.y<<") -> ("<<p.b.x<<","<<p.b.y<<") - "<<(p.clear?"Unobstructed":"Touches glade")<<"\n";
        }
        std::cout << "[*] Seating ancient trees outside hedges...\n";
        std::cout << "[*] Layout summary: Trees Placed="<<treeInstances.size()<<" (S="<<sPlaced<<" M="<<mPlaced<<" T="<<tPlaced<<") Paths="<<layoutPaths.size()<<" Hedges="<<hedgeWedgeTris.size()<<"\n\n";
        if ((int)treeInstances.size() < targetTotal) {
            std::cout << "[Guard] Not all requested trees could be placed due to constraints; placed "<<treeInstances.size()<<" of "<<targetTotal<<".\n";
        }
        std::cout << "=== CONTROLS ===\n";
        std::cout << "V           : Toggle 2D / 3D realms\n";
        std::cout << "W/A/S/D     : Wander (3D)\n";
        std::cout << "P           : Cycle path style (visual only)\n";
        std::cout << "[/]         : Fountain radius pixel ring\n";
        std::cout << "T/M         : Cycle ground texture\n";
        std::cout << "I/O,K/L,J,U : Scale / rotate models (3D)\n";
        std::cout << "Mouse L     : Plant extra tree (both views)\n";
        std::cout << "ESC         : Exit\n";
        std::cout << "\nBootstrapping complete. Summoning window...\n";
        layoutGenerated = true;
    }
    // GLFW / GLEW Init
    if (!glfwInit()) return -1;
    GLFWwindow* win = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Enchanted Forest", NULL, NULL);
    if (!win) return -1;
    glfwMakeContextCurrent(win);
    glfwSetInputMode(win, GLFW_STICKY_KEYS, GLFW_TRUE);
    glfwSetMouseButtonCallback(win, mouseCallback);
    glewInit();

        // Show view name in window title
        {
            std::string title = std::string("Enchanted Forest [") + (currentView==VIEW_3D?"3D":"2D") + "]";
            glfwSetWindowTitle(win, title.c_str());
        }

    glEnable(GL_DEPTH_TEST);

    // Load shaders & models
    // NOTE: vertex shader is stored as 'forest.vert'.
    shaderProgram = compileShaderFromFile("forest.vert", "fragment_shader.glsl");
    glUseProgram(shaderProgram);

    // Fountain: load OBJ model and set scale to 0.5 of original size; texture mapped via fountain.png
    fountainModel = loadModel("Models/fountain.obj", "Models/fountain.png");
    useProceduralFountain = fountainModel.meshes.empty();
    // Set position; we will align Y so base sits at ground
    fountainModel.position = glm::vec3(0.0f, 0.0f, 0.0f);
    {
        // For OBJ fountain: uniform 0.5x scale of original
        if (!useProceduralFountain) {
            float s = 0.5f;
            fountainModel.scale = glm::vec3(s);
            // Align base to ground: lowest vertex at y=0 after scaling
            float baseY = -fountainModel.minY * s;
            fountainModel.position.y = baseY;
        }
        // For procedural fountain: scale factor represents overall size; set to 0.5
        fountainScale = 0.5f;
    }

    // Set hedge (wedge) height to 0.5 of the fountain height
    {
        float fountainHeightForHedges = 0.0f;
        if (!useProceduralFountain) {
            float s = fountainModel.scale.y;
            fountainHeightForHedges = (fountainModel.maxY - fountainModel.minY) * s;
        } else {
            // Procedural fountain total height approx: 0.30 + 0.60 + 0.12 + 0.20 = 1.22 times scale
            fountainHeightForHedges = 1.22f * fountainScale;
        }
        hedgeHeight = 0.5f * fountainHeightForHedges;
    }
    // Procedural ground plane + textures
    createGroundPlane();
    extern GLuint loadTexture(const char* path);
    groundTextures[0] = loadTexture("Models/grass.png");
    groundTextures[1] = loadTexture("Models/moss.png");
    groundTextures[2] = loadTexture("Models/purple.png");
    pathTexture = loadTexture("Models/path.png");
    // 2D sprite textures for non-OBJ 2D view
    treeSpriteTex = 0; // no 2D sprite needed for trees
    fountainSpriteTex = loadTexture("Models/fountain.png");
    // Procedural tree textures
    trunkTexture = loadTexture("Models/trunk.png");
    leavesTexture = loadTexture("Models/leaves.png");

    // Fixed path width (for stylized path mesh); accurate path mesh uses 1× tile per grid step
    pathHalfWidth = 0.3f;
    updatePathMesh(pathStyle);
    updateAccuratePathMesh();
    updateFountainRing(fountainScale);


    initFireflies(30);
    createCylinder(0.08f, 24);
    createCone(0.20f, 24);
    buildHedgeMeshes();

    glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)SCR_WIDTH/(float)SCR_HEIGHT, 0.1f, 100.0f);

    float time = 0.0f;
    while (!glfwWindowShouldClose(win)) {
        // Distinct background colors for views
        if (currentView == VIEW_3D) {
            glClearColor(0.1f, 0.15f, 0.2f, 1.0f); // night forest tone
        } else {
            glClearColor(0.07f, 0.07f, 0.09f, 1.0f); // dark pixel blueprint background
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Camera movement (3D only)
        float speed = 0.1f;
        if (currentView == VIEW_3D) {
            if (glfwGetKey(win, GLFW_KEY_W)) cameraPos += speed * cameraFront;
            if (glfwGetKey(win, GLFW_KEY_S)) cameraPos -= speed * cameraFront;
            if (glfwGetKey(win, GLFW_KEY_A)) cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * speed;
            if (glfwGetKey(win, GLFW_KEY_D)) cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * speed;
        }

        // Camera rotation (yaw/pitch) when in 3D
        if (currentView == VIEW_3D) {
            // Reduced rotation speed for finer per-key control
            float rotSpeed = 0.3f;
            if (glfwGetKey(win, GLFW_KEY_LEFT)) yawDeg -= rotSpeed;
            if (glfwGetKey(win, GLFW_KEY_RIGHT)) yawDeg += rotSpeed;
            if (glfwGetKey(win, GLFW_KEY_UP)) pitchDeg += rotSpeed;
            if (glfwGetKey(win, GLFW_KEY_DOWN)) pitchDeg -= rotSpeed;
            if (pitchDeg > 89.0f) pitchDeg = 89.0f; if (pitchDeg < -89.0f) pitchDeg = -89.0f;
            float yawRad = glm::radians(yawDeg); float pitchRad = glm::radians(pitchDeg);
            cameraFront = glm::normalize(glm::vec3(
                cos(yawRad) * cos(pitchRad),
                sin(pitchRad),
                sin(yawRad) * cos(pitchRad)
            ));
        }
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

        // Inputs and actions
        if (isKeyPressedOnce(win, GLFW_KEY_V)) {
            currentView = (currentView==VIEW_3D?VIEW_2D:VIEW_3D);
            if (currentView == VIEW_3D) showBlueprint = false; else showBlueprint = true;
            std::cout << "[Action] View toggled: " << (currentView==VIEW_3D?"3D":"2D") << "\n";
            if (currentView == VIEW_3D) {
                // Keep current fountainScale (fixed to match OBJ scale request)
                updateFountainRing(fountainScale);
                updatePathMesh(pathStyle);
                updateAccuratePathMesh();
                // Rebuild hedge meshes with existing radii; do not override radii on toggle
                wedgeVAO1 = wedgeVBO1 = wedgeEBO1 = 0; wedgeIdx1 = 0;
                wedgeVAO2 = wedgeVBO2 = wedgeEBO2 = 0; wedgeIdx2 = 0;
                // Rebuild hedge meshes with new radii
                buildHedgeMeshes();
                std::cout << "[Action] Hedge meshes rebuilt for 3D view\n";
            }
            // Update title with current view
            {
                std::string t = std::string("Enchanted Forest [") + (currentView==VIEW_3D?"3D":"2D") + "]";
                glfwSetWindowTitle(win, t.c_str());
            }
        }
        // Cycle path style (visual only for stylized demo path mesh)
        if (isKeyPressedOnce(win, GLFW_KEY_P)) {
            pathStyle = (pathStyle + 1) % 3;
            updatePathMesh(pathStyle);
            debugFlashPing(glm::vec3(0.6f, 0.8f, 1.0f));
            std::cout << "[Action] Path style cycled to " << pathStyle << " (visual-only mesh)\n";
        }
        // Cycle ground texture forward/backward
        if (isKeyPressedOnce(win, GLFW_KEY_T)) { currentGroundTex = (currentGroundTex + 1) % 3; std::cout << "[Action] Ground texture -> index " << currentGroundTex << "\n"; }
        if (isKeyPressedOnce(win, GLFW_KEY_M)) { currentGroundTex = (currentGroundTex + 2) % 3; std::cout << "[Action] Ground texture <- index " << currentGroundTex << "\n"; }
        // Adjust fountain radius in 2D (affects overlay annulus and 3D ring build)
        if (isKeyPressedOnce(win, GLFW_KEY_LEFT_BRACKET)) {
            fountainRadius = std::max(10, fountainRadius - 2);
            updateAccuratePathMesh();
            updateFountainRing(fountainScale);
            std::cout << "[Action] Fountain radius decreased: " << fountainRadius << " px\n";
        }
        if (isKeyPressedOnce(win, GLFW_KEY_RIGHT_BRACKET)) {
            fountainRadius = std::min(240, fountainRadius + 2);
            updateAccuratePathMesh();
            updateFountainRing(fountainScale);
            std::cout << "[Action] Fountain radius increased: " << fountainRadius << " px\n";
        }

        // 3D rendering pass
        if (currentView == VIEW_3D) {
            setCommonUniforms(shaderProgram, view, projection, cameraPos);

            // Ground
            glUseProgram(shaderProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, groundTextures[currentGroundTex]);
            glUniform1i(glGetUniformLocation(shaderProgram, "texture_diffuse1"), 0);
            glm::mat4 groundModel(1.0f);
            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &groundModel[0][0]);
            glBindVertexArray(groundVAO);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);

            // Paths (always render accurate user paths when available). Fallback to stylized mesh.
            glBindTexture(GL_TEXTURE_2D, pathTexture);
            if (layoutPathVAO && layoutPathIndexCount > 0) {
                glBindVertexArray(layoutPathVAO);
                glDrawElements(GL_TRIANGLES, layoutPathIndexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            } else if (pathVAO) {
                glBindVertexArray(pathVAO);
                GLint eboSize = 0; glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &eboSize);
                GLsizei count = (GLsizei)(eboSize / sizeof(unsigned int));
                glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }

            // Fountain: draw OBJ if available, else procedural fallback. Fountain rotates in yaw only.
            if (useProceduralFountain) {
                drawProceduralFountain(shaderProgram, view, projection);
            } else {
                // Apply yaw-only rotation and scale to OBJ fountain
                fountainModel.rotation.y = glm::radians(fountainYawDeg);
                fountainModel.rotation.x = 0.0f;
                fountainModel.rotation.z = 0.0f;
                fountainModel.scale = glm::vec3(0.5f * fountainGlobalScale);
                drawModel(fountainModel, view, projection);
            }

            // Procedural trees: trunk (textured) + leaves (textured)
            for (auto &ti : treeInstances) {
                // Increase tree scaling so they are not too small vs fountain
                float fScale = fountainScale;
                float base = (ti.size==Small?0.9f:(ti.size==Medium?1.2f:1.7f));
                float trunkH = base * (fScale * 3.0f) * treeScaleFactor;
                float trunkR = base * (0.10f * fScale * 1.2f) * treeScaleFactor;
                float coneH  = base * (fScale * 2.4f) * treeScaleFactor;
                float coneR  = base * (0.24f * fScale * 1.8f) * treeScaleFactor;

                // trunk (apply tree yaw rotation and scale)
                glm::mat4 modelM = glm::translate(glm::mat4(1.0f), glm::vec3(ti.pos.x, 0.0f, ti.pos.y));
                modelM = glm::rotate(modelM, glm::radians(treeYawDeg), glm::vec3(0,1,0));
                // scale factors incorporate treeGlobalScale
                modelM = glm::scale(modelM, glm::vec3((trunkR*treeGlobalScale)/0.08f, (trunkH*treeGlobalScale)/1.0f, (trunkR*treeGlobalScale)/0.08f));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &modelM[0][0]);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, trunkTexture);
                glUniform1i(glGetUniformLocation(shaderProgram, "texture_diffuse1"), 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "solidMode"), 0);
                glBindVertexArray(trunkVAO);
                glDrawElements(GL_TRIANGLES, trunkIndexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                // foliage cone on top (apply same tree rotation and scale)
                glm::mat4 coneM = glm::translate(glm::mat4(1.0f), glm::vec3(ti.pos.x, trunkH*treeGlobalScale, ti.pos.y));
                coneM = glm::rotate(coneM, glm::radians(treeYawDeg), glm::vec3(0,1,0));
                coneM = glm::scale(coneM, glm::vec3((coneR*treeGlobalScale)/0.20f, (coneH*treeGlobalScale)/1.0f, (coneR*treeGlobalScale)/0.20f));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &coneM[0][0]);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, leavesTexture);
                glUniform1i(glGetUniformLocation(shaderProgram, "texture_diffuse1"), 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "solidMode"), 0);
                glBindVertexArray(coneVAO);
                glDrawElements(GL_TRIANGLES, coneIndexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                glUniform1i(glGetUniformLocation(shaderProgram, "solidMode"), 0);
            }

            // Star hedge wedges
            drawHedgeWedges(shaderProgram);

            // Ring (annulus) with world-aligned UV tiling (path.png 1:1 per grid cell)
            if (ringVAO && ringIndexCount > 0) {
                glm::mat4 ringModel = glm::mat4(1.0f);
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &ringModel[0][0]);
                glBindTexture(GL_TEXTURE_2D, pathTexture);
                glBindVertexArray(ringVAO);
                glDrawElements(GL_TRIANGLES, ringIndexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }

            // Fireflies
            drawFireflies(shaderProgram, view, projection, time);
        }

        // In 2D view, draw coded pixel glyphs instead of OBJ models / textures
        if (currentView == VIEW_2D) {
            drawPixelObjects2D();
        }

        // (Debug cubes removed)

        // (Removed NDC triangle debug)


        // Draw 2D blueprint overlay only in VIEW_2D
        drawBlueprintOverlay();

        // Separate model controls (3D only)
        if (currentView == VIEW_3D) {
            // Trees: scale with I/O, yaw left with J
            if (glfwGetKey(win, GLFW_KEY_I)) { treeGlobalScale = std::min(3.0f, treeGlobalScale + 0.01f); std::cout << "[Action] Trees scale + -> " << treeGlobalScale << "\n"; }
            if (glfwGetKey(win, GLFW_KEY_O)) { treeGlobalScale = std::max(0.2f, treeGlobalScale - 0.01f); std::cout << "[Action] Trees scale - -> " << treeGlobalScale << "\n"; }
            if (glfwGetKey(win, GLFW_KEY_J)) { treeYawDeg -= 0.8f; std::cout << "[Action] Trees yaw left -> " << treeYawDeg << " deg\n"; }

            // Fountain: scale with K/L, yaw right with U
            if (glfwGetKey(win, GLFW_KEY_K)) {
                fountainGlobalScale = std::min(3.0f, fountainGlobalScale + 0.01f);
                hedgeGlobalScale = fountainGlobalScale * 0.8f;
                updateAccuratePathMesh();
                updateFountainRing(fountainScale);
                // Rebuild hedge meshes to reflect scale visually
                wedgeVAO1 = wedgeVBO1 = wedgeEBO1 = 0; wedgeIdx1 = 0;
                wedgeVAO2 = wedgeVBO2 = wedgeEBO2 = 0; wedgeIdx2 = 0;
                buildHedgeMeshes();
                // Preserve per-tree fountain gap: r_new = fountainFootprintNew + gap_i
                float newFountainFoot = fountainScale * fountainGlobalScale * 1.1f;
                float newHedgeOuter   = wedgeROuter2 * hedgeGlobalScale;
                for (size_t i=0;i<treeInstances.size();++i) {
                    glm::vec2 p(treeInstances[i].pos.x, treeInstances[i].pos.y);
                    float r = glm::length(p);
                    float desiredR = newFountainFoot + (i < treeFountainGap.size()? treeFountainGap[i] : 0.0f);
                    // Ensure we also honor stored hedge margin if it pushes further out
                    if (i < treeOuterMargin.size()) {
                        float hedgeDesired = newHedgeOuter + treeOuterMargin[i];
                        if (hedgeDesired > desiredR) desiredR = hedgeDesired;
                    }
                    if (r > 1e-5f) {
                        glm::vec2 dir = p / r;
                        treeInstances[i].pos = dir * desiredR;
                    }
                }
                std::cout << "[Action] Fountain scale + -> " << fountainGlobalScale << " (paths/ring/hedges updated)\n";
            }
            if (glfwGetKey(win, GLFW_KEY_L)) {
                fountainGlobalScale = std::max(0.2f, fountainGlobalScale - 0.01f);
                hedgeGlobalScale = fountainGlobalScale * 0.8f;
                updateAccuratePathMesh();
                updateFountainRing(fountainScale);
                wedgeVAO1 = wedgeVBO1 = wedgeEBO1 = 0; wedgeIdx1 = 0;
                wedgeVAO2 = wedgeVBO2 = wedgeEBO2 = 0; wedgeIdx2 = 0;
                buildHedgeMeshes();
                // Preserve per-tree fountain gap when shrinking
                float newFountainFoot = fountainScale * fountainGlobalScale * 1.1f;
                float newHedgeOuter   = wedgeROuter2 * hedgeGlobalScale;
                for (size_t i=0;i<treeInstances.size();++i) {
                    glm::vec2 p(treeInstances[i].pos.x, treeInstances[i].pos.y);
                    float r = glm::length(p);
                    float desiredR = newFountainFoot + (i < treeFountainGap.size()? treeFountainGap[i] : 0.0f);
                    if (i < treeOuterMargin.size()) {
                        float hedgeDesired = newHedgeOuter + treeOuterMargin[i];
                        if (hedgeDesired > desiredR) desiredR = hedgeDesired; // keep trees outside hedge margin
                    }
                    if (r > 1e-5f) {
                        glm::vec2 dir = p / r;
                        treeInstances[i].pos = dir * desiredR;
                    }
                }
                std::cout << "[Action] Fountain scale - -> " << fountainGlobalScale << " (paths/ring/hedges updated)\n";
            }
            if (glfwGetKey(win, GLFW_KEY_U)) { fountainYawDeg += 0.8f; std::cout << "[Action] Fountain yaw right -> " << fountainYawDeg << " deg\n"; }
        }

        // Collision guard: prevent fountain scaling into hedges; scale hedges with fountain and push/pull trees to follow
        {
            // Effective fountain footprint radius (approx) and scaled hedge inner/outer radii
            float fFootR = fountainScale * fountainGlobalScale * 1.1f; // ~footprint
            float hedgeInnerScaled = wedgeRInner1 * hedgeGlobalScale;
            float hedgeOuterScaled = wedgeROuter2 * hedgeGlobalScale;
            // If fountain would hit inner hedge band, clamp back
            if (fFootR >= hedgeInnerScaled * 0.95f) {
                float prev = fountainGlobalScale;
                fountainGlobalScale = std::max(0.2f, (hedgeInnerScaled*0.95f) / (fountainScale * 1.1f));
                if (fabsf(prev - fountainGlobalScale) > 1e-6f) {
                    std::cout << "[Guard] Fountain scale clamped from " << prev << " to " << fountainGlobalScale << " to avoid hedge collision\n";
                }
            }
            // Follow fountain scale for hedges
            hedgeGlobalScale = fountainGlobalScale * 0.8f;
            // Push trees outward if now inside outer hedge disk; pull inward if hedge shrinks
            static float lastHedgeOuterScaled = wedgeROuter2; // initial reference
            float newHedgeOuterScaled = wedgeROuter2 * hedgeGlobalScale;
            int pushed=0, pulled=0;
            for (auto &ti : treeInstances) {
                glm::vec2 p(ti.pos.x, ti.pos.y);
                float r = glm::length(p);
                float minR = newHedgeOuterScaled + 0.15f;
                if (newHedgeOuterScaled >= lastHedgeOuterScaled) {
                    // Hedge expanded: ensure trees are at least just outside the new disk
                    if (r < minR && r > 1e-4f) {
                        glm::vec2 dir = p / r;
                        // Preserve per-tree margin if available
                        size_t idx = &ti - &treeInstances[0];
                        float margin = (idx < treeOuterMargin.size() ? treeOuterMargin[idx] : 0.15f);
                        ti.pos = dir * std::max(minR, newHedgeOuterScaled + margin);
                        pushed++;
                    }
                } else {
                    // Hedge shrank: keep constant margin from new outer radius
                    if (r > 1e-4f) {
                        size_t idx = &ti - &treeInstances[0];
                        float margin = (idx < treeOuterMargin.size() ? treeOuterMargin[idx] : 0.15f);
                        float newR = std::max(minR, newHedgeOuterScaled + margin);
                        glm::vec2 dir = p / r;
                        ti.pos = dir * newR;
                        pulled++;
                    }
                }
            }
            if (pushed>0) std::cout << "[Guard] Trees pushed outward: " << pushed << "\n";
            if (pulled>0) std::cout << "[Guard] Trees pulled inward: " << pulled << "\n";
            lastHedgeOuterScaled = newHedgeOuterScaled;
        }
        // ESC to exit
        if (isKeyPressedOnce(win, GLFW_KEY_ESCAPE)) {
            std::cout << "[Action] ESC pressed: exiting\n";
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        // R to fully reset: camera, view mode, all transforms, scalings, and visual state
        if (isKeyPressedOnce(win, GLFW_KEY_R)) {
            // Reset camera and orientation
            cameraPos   = glm::vec3(0.0f, 2.0f, 10.0f);
            cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
            cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);
            yawDeg   = -90.0f;
            pitchDeg = 0.0f;
            // Reset view mode
            currentView = VIEW_3D;
            std::string t = std::string("Enchanted Forest [") + (currentView==VIEW_3D?"3D":"2D") + "]";
            glfwSetWindowTitle(win, t.c_str());
            // Reset user-tweakable styles/params to initial values
            pathStyle = 0;
            currentGroundTex = 0;
            fountainRadius = 60;
            // Reset model transforms (remove all scalings/rotations)
            treeGlobalScale = 1.0f;
            treeYawDeg = 0.0f;
            fountainGlobalScale = 1.0f;
            fountainYawDeg = 0.0f;
            hedgeGlobalScale = 0.8f;
            // Reset fountain base scale/state
            if (!useProceduralFountain) {
                fountainModel.scale = glm::vec3(0.5f * fountainGlobalScale);
                float baseY = -fountainModel.minY * fountainModel.scale.y;
                fountainModel.position.y = baseY;
                fountainModel.rotation = glm::vec3(0.0f);
            } else {
                fountainScale = 0.5f; // procedural base scale
            }
            // Rebuild hedges with base radii
            wedgeVAO1 = wedgeVBO1 = wedgeEBO1 = 0; wedgeIdx1 = 0;
            wedgeVAO2 = wedgeVBO2 = wedgeEBO2 = 0; wedgeIdx2 = 0;
            buildHedgeMeshes();
            // Recompute dependent meshes
            updatePathMesh(pathStyle);
            updateAccuratePathMesh();
            updateFountainRing(fountainScale);
            // Recompute per-tree gap baselines after full reset
            treeOuterMargin.clear();
            treeFountainGap.clear();
            float resetOuter = wedgeROuter2 * hedgeGlobalScale;
            float resetFountainFoot = fountainScale * fountainGlobalScale * 1.1f;
            for (auto &ti : treeInstances) {
                float r = glm::length(glm::vec2(ti.pos.x, ti.pos.y));
                treeOuterMargin.push_back(std::max(0.0f, r - resetOuter));
                treeFountainGap.push_back(std::max(0.0f, r - resetFountainFoot));
            }
            debugFlashPing(glm::vec3(0.7f, 0.9f, 0.6f));
            std::cout << "[Action] Full reset: camera, view, styles, transforms, and meshes restored to start\n";
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
        time += 0.01f;
        if (debugFlash > 0.0f) debugFlash -= 0.016f;
    }

    glfwTerminate();
    return 0;
}
