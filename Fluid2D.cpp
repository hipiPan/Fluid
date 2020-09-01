#include "Fluid2D.h"
#include "glad/glad.h"
#include <glm.hpp>
#include <memory>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

#define SAFE_DELETE(x) \
    { \
        delete x; \
        x = nullptr; \
    }

#define SAFE_DELETE_ARRAY(x) \
    { \
        delete[] x; \
        x = nullptr; \
    }
static bool readFileData(const std::string& path, std::string& out);
static void checkShaderCompileErrors(GLuint id);
static void checkProgramCompileErrors(GLuint id);
static uint32_t createProgram(const char* vertexSource, const char* fragmentSource);

float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
        1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
        1.0f, -1.0f,  1.0f, 0.0f,
        1.0f,  1.0f,  1.0f, 1.0f
};

Framebuffer::Framebuffer(uint32_t w, uint32_t h, uint32_t internalFormat, uint32_t format, uint32_t type, uint32_t param) {
    glCreateTextures(GL_TEXTURE_2D, 1, &mTextureID);
    glBindTexture(GL_TEXTURE_2D, mTextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, param);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, param);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
    glCreateFramebuffers(1, &mFramebufferID);
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebufferID);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTextureID, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Framebuffer::~Framebuffer() {
    glDeleteTextures(1, &mTextureID);
    glDeleteFramebuffers(1, &mFramebufferID);
}

DoubleFramebuffer::DoubleFramebuffer(uint32_t w, uint32_t h, uint32_t internalFormat, uint32_t format, uint32_t type, uint32_t param) {
    mFramebufferA = new Framebuffer(w, h, internalFormat, format, type, param);
    mFramebufferB = new Framebuffer(w, h, internalFormat, format, type, param);
}

DoubleFramebuffer::~DoubleFramebuffer() {
    SAFE_DELETE(mFramebufferA);
    SAFE_DELETE(mFramebufferB);
}

Fluid2D::Fluid2D(int size) {
    mSize = size;
    mBoundary = std::make_shared<Framebuffer>(mSize, mSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST);
    mDivergence = std::make_shared<Framebuffer>(mSize, mSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST);
    mDensity = std::make_shared<DoubleFramebuffer>(mSize, mSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    mVelocity = std::make_shared<DoubleFramebuffer>(mSize, mSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    mTemperature = std::make_shared<DoubleFramebuffer>(mSize, mSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    mPressure = std::make_shared<DoubleFramebuffer>(mSize, mSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST);

    glGenVertexArrays(1, &mQuadVAO);
    glGenBuffers(1, &mQuadVBO);
    glBindVertexArray(mQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    std::string vs, fs;
    readFileData("Res/Fluid2D/common.vs", vs);
    readFileData("Res/Fluid2D/common.fs", fs);
    mCommonProgram = createProgram(vs.c_str(), fs.c_str());
    readFileData("Res/Fluid2D/boundary.fs", fs);
    mBoundaryProgram = createProgram(vs.c_str(), fs.c_str());
    readFileData("Res/Fluid2D/splat.fs", fs);
    mSplatProgram = createProgram(vs.c_str(), fs.c_str());
    readFileData("Res/Fluid2D/advection.fs", fs);
    mAdvectionProgram = createProgram(vs.c_str(), fs.c_str());
    readFileData("Res/Fluid2D/buoyancy.fs", fs);
    mBuoyancyProgram = createProgram(vs.c_str(), fs.c_str());
    readFileData("Res/Fluid2D/divergence.fs", fs);
    mDivergenceProgram = createProgram(vs.c_str(), fs.c_str());
    readFileData("Res/Fluid2D/jacobi.fs", fs);
    mJacobiProgram = createProgram(vs.c_str(), fs.c_str());
    readFileData("Res/Fluid2D/subtractGradient.fs", fs);
    mSubtractGradientProgram = createProgram(vs.c_str(), fs.c_str());

    // fill boundary
    glBindFramebuffer(GL_FRAMEBUFFER, mBoundary->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mBoundaryProgram);
    glUniform2fv(glGetUniformLocation(mBoundaryProgram, "inverseSize"), 1, &glm::vec2(1.0f / mSize, 1.0f / mSize)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Fluid2D::~Fluid2D() {
    glDeleteVertexArrays(1, &mQuadVAO);
    glDeleteBuffers(1, &mQuadVBO);
    glDeleteProgram(mCommonProgram);
    glDeleteProgram(mBoundaryProgram);
    glDeleteProgram(mSplatProgram);
    glDeleteProgram(mAdvectionProgram);
    glDeleteProgram(mBuoyancyProgram);
    glDeleteProgram(mDivergenceProgram);
    glDeleteProgram(mJacobiProgram);
    glDeleteProgram(mSubtractGradientProgram);
}

void Fluid2D::update(float dt) {
    advect(dt);
    buoyancy(dt);
    divergence();
    pressure();
    subtractGradient();

    splat(0.5, 0.0, 0.0, 0.0);
}

void Fluid2D::render() {
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mCommonProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mBoundaryProgram);
    glUniform1i(glGetUniformLocation(mCommonProgram, "u_boundary"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mDensity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mCommonProgram, "u_density"), 1);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Fluid2D::buoyancy(float dt) {
    glBindFramebuffer(GL_FRAMEBUFFER, mVelocity->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mBuoyancyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mVelocity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mBuoyancyProgram, "u_velocity"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mTemperature->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mBuoyancyProgram, "u_temperature"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mDensity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mBuoyancyProgram, "u_density"), 2);

    glUniform1f(glGetUniformLocation(mBuoyancyProgram, "u_timeStep"), dt);
    glUniform1f(glGetUniformLocation(mBuoyancyProgram, "u_ambientTemperature"), 0.0f);
    glUniform1f(glGetUniformLocation(mBuoyancyProgram, "u_sigma"), 1.0f);
    glUniform1f(glGetUniformLocation(mBuoyancyProgram, "u_kappa"), 0.05f);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mVelocity->swap();
}

void Fluid2D::advect(float dt) {
    glBindFramebuffer(GL_FRAMEBUFFER, mVelocity->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mAdvectionProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mBoundary->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_boundary"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mVelocity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_source"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mVelocity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_velocity"), 2);
    glUniform1f(glGetUniformLocation(mAdvectionProgram, "u_timeStep"), dt);
    glUniform1f(glGetUniformLocation(mAdvectionProgram, "u_dissipation"), 0.99f);
    glUniform2fv(glGetUniformLocation(mAdvectionProgram, "u_inverseSize"), 1, &glm::vec2(1.0f / mSize, 1.0f / mSize)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mVelocity->swap();

    glBindFramebuffer(GL_FRAMEBUFFER, mTemperature->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mAdvectionProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mBoundary->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_boundary"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mTemperature->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_source"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mVelocity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_velocity"), 2);
    glUniform1f(glGetUniformLocation(mAdvectionProgram, "u_timeStep"), dt);
    glUniform1f(glGetUniformLocation(mAdvectionProgram, "u_dissipation"), 0.99f);
    glUniform2fv(glGetUniformLocation(mAdvectionProgram, "u_inverseSize"), 1, &glm::vec2(1.0f / mSize, 1.0f / mSize)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mTemperature->swap();

    glBindFramebuffer(GL_FRAMEBUFFER, mDensity->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mAdvectionProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mBoundary->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_boundary"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mDensity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_source"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mVelocity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mAdvectionProgram, "u_velocity"), 2);
    glUniform1f(glGetUniformLocation(mAdvectionProgram, "u_timeStep"), dt);
    glUniform1f(glGetUniformLocation(mAdvectionProgram, "u_dissipation"), 0.9999f);
    glUniform2fv(glGetUniformLocation(mAdvectionProgram, "u_inverseSize"), 1, &glm::vec2(1.0f / mSize, 1.0f / mSize)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mDensity->swap();
}

void Fluid2D::divergence() {
    glBindFramebuffer(GL_FRAMEBUFFER, mDivergence->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mDivergenceProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mBoundary->getTexture());
    glUniform1i(glGetUniformLocation(mDivergenceProgram, "u_boundary"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mVelocity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mDivergenceProgram, "u_velocity"), 1);
    glUniform2fv(glGetUniformLocation(mDivergenceProgram, "u_inverseSize"), 1, &glm::vec2(1.0f / mSize, 1.0f / mSize)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Fluid2D::pressure() {
    // clear
    glBindFramebuffer(GL_FRAMEBUFFER, mPressure->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mPressure->swap();

    // iteration
    for (int i = 0; i < 30; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, mPressure->getWriteFramebuffer()->getFramebuffer());
        glViewport(0, 0, mSize, mSize);
        glUseProgram(mJacobiProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mBoundary->getTexture());
        glUniform1i(glGetUniformLocation(mJacobiProgram, "u_boundary"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mPressure->getReadFramebuffer()->getTexture());
        glUniform1i(glGetUniformLocation(mJacobiProgram, "u_pressure"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, mDivergence->getTexture());
        glUniform1i(glGetUniformLocation(mJacobiProgram, "u_divergence"), 2);
        glUniform1f(glGetUniformLocation(mJacobiProgram, "u_alpha"), -1.0 * 1.0);
        glUniform1f(glGetUniformLocation(mJacobiProgram, "u_inverseBeta"), 0.25f);
        glUniform2fv(glGetUniformLocation(mJacobiProgram, "u_inverseSize"), 1, &glm::vec2(1.0f / mSize, 1.0f / mSize)[0]);
        glBindVertexArray(mQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        mPressure->swap();
    }
}

void Fluid2D::subtractGradient() {
    glBindFramebuffer(GL_FRAMEBUFFER, mVelocity->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mSubtractGradientProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mBoundary->getTexture());
    glUniform1i(glGetUniformLocation(mSubtractGradientProgram, "u_boundary"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mPressure->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mSubtractGradientProgram, "u_pressure"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mVelocity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mSubtractGradientProgram, "u_velocity"), 2);
    glUniform1f(glGetUniformLocation(mSubtractGradientProgram, "u_gradientScale"), 1.0f);
    glUniform2fv(glGetUniformLocation(mSubtractGradientProgram, "u_inverseSize"), 1, &glm::vec2(1.0f / mSize, 1.0f / mSize)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mVelocity->swap();
}

void Fluid2D::splat(float x, float y, float dx, float dy) {
    glBindFramebuffer(GL_FRAMEBUFFER, mTemperature->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mSplatProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTemperature->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mSplatProgram, "u_texture"), 0);
    glUniform1f(glGetUniformLocation(mSplatProgram, "u_radius"), 0.1);
    glUniform2fv(glGetUniformLocation(mSplatProgram, "u_point"), 1, &glm::vec2(x, y)[0]);
    glUniform3fv(glGetUniformLocation(mSplatProgram, "u_value"), 1, &glm::vec3(10.0, 0.0f, 0.0f)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mTemperature->swap();

    glBindFramebuffer(GL_FRAMEBUFFER, mDensity->getWriteFramebuffer()->getFramebuffer());
    glViewport(0, 0, mSize, mSize);
    glUseProgram(mSplatProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mDensity->getReadFramebuffer()->getTexture());
    glUniform1i(glGetUniformLocation(mSplatProgram, "u_texture"), 0);
    glUniform1f(glGetUniformLocation(mSplatProgram, "u_radius"), 0.1);
    glUniform2fv(glGetUniformLocation(mSplatProgram, "u_point"), 1, &glm::vec2(x, y)[0]);
    glUniform3fv(glGetUniformLocation(mSplatProgram, "u_value"), 1, &glm::vec3(1.0f, 0.0f, 0.0f)[0]);
    glBindVertexArray(mQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mDensity->swap();
}

static void checkShaderCompileErrors(GLuint id) {
    GLint success;
    GLchar log[1024];
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(id, 1024, NULL, log);
        printf("shader compile error : %s \n", log);
    }
}

static void checkProgramCompileErrors(GLuint id) {
    GLint success;
    GLchar log[1024];
    glGetProgramiv(id, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(id, 1024, NULL, log);
        printf("program compile error : %s \n", log);
    }
}

uint32_t createProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vertexID, fragmentID;
    vertexID = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexID, 1, &vertexSource, NULL);
    glCompileShader(vertexID);
    checkShaderCompileErrors(vertexID);

    fragmentID = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentID, 1, &fragmentSource, NULL);
    glCompileShader(fragmentID);
    checkShaderCompileErrors(fragmentID);

    GLuint programID;
    programID = glCreateProgram();
    glAttachShader(programID, vertexID);
    glAttachShader(programID, fragmentID);
    glLinkProgram(programID);
    checkProgramCompileErrors(programID);

    glDeleteShader(vertexID);
    glDeleteShader(fragmentID);

    return programID;
}

static bool readFileData(const std::string& path, std::string& out) {
    std::istream* stream = &std::cin;
    std::ifstream file;

    file.open(path, std::ios_base::binary);
    stream = &file;
    if (file.fail()) {
        printf("cannot open input file %s \n", path.c_str());
        return false;
    }
    out = std::string((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
    return true;
}