#pragma once
#include <GL/glew.h>
#include <string>

std::string readFile(const char* filePath);
GLuint compileShaderFromFile(const char* vertexPath, const char* fragmentPath);
