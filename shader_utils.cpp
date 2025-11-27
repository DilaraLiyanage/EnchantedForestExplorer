#include "shader_utils.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string readFile(const char* filePath) {
    std::ifstream file(filePath);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

GLuint compileShaderFromFile(const char* vertexPath, const char* fragmentPath) {
    std::string vertexCode = readFile(vertexPath);
    std::string fragmentCode = readFile(fragmentPath);
    const char* vSrc = vertexCode.c_str();
    const char* fSrc = fragmentCode.c_str();

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vSrc, NULL);
    glCompileShader(vertexShader);
    {
        GLint success = 0; glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            GLint len = 0; glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0'); glGetShaderInfoLog(vertexShader, len, NULL, &log[0]);
            std::cerr << "Vertex shader compile error (" << vertexPath << "):\n" << log << std::endl;
        }
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fSrc, NULL);
    glCompileShader(fragmentShader);
    {
        GLint success = 0; glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            GLint len = 0; glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0'); glGetShaderInfoLog(fragmentShader, len, NULL, &log[0]);
            std::cerr << "Fragment shader compile error (" << fragmentPath << "):\n" << log << std::endl;
        }
    }

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    {
        GLint success = 0; glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            GLint len = 0; glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0'); glGetProgramInfoLog(shaderProgram, len, NULL, &log[0]);
            std::cerr << "Shader link error (" << vertexPath << ", " << fragmentPath << "):\n" << log << std::endl;
        }
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return shaderProgram;
}
