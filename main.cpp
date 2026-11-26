#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <limits>
#include <filesystem>
#include <unordered_map>
#include <cstddef>
#include <cstdlib>
#include <cctype>
#include <memory>

namespace fs = std::filesystem;

static const unsigned int SCR_WIDTH = 1280;
static const unsigned int SCR_HEIGHT = 720;

static const fs::path WARRIOR_DIR = R"(C:\Users\olsos\OneDrive\gltf)";
static const fs::path WOLF_DIR = R"(C:\Users\olsos\OneDrive\glft 2)";

// Carpeta del gltf del suelo (donde esta scene.gltf, el .bin y las texturas).
static const fs::path GROUND_DIR = R"(C:\Users\olsos\OneDrive\gltf suelo)";

// Tamano del suelo: la dimension mas grande del modelo quedara en estas unidades.
static const float GROUND_SIZE = 40.0f;

// Ajuste vertical del suelo (negativo = lo baja). Si los pies se hunden, bajalo mas.
static const float GROUND_Y_OFFSET = -0.01f;

// true = usa el gltf del suelo (GROUND_DIR). false = usa un piso plano con GROUND_TEX.
static const bool USE_GLTF_GROUND = false;

// Textura del piso plano (jpg, jpe o png). Si no existe, el piso se dibuja de color gris.
static const fs::path GROUND_TEX = R"(C:\Users\olsos\Downloads\suelo.jpe)";

static float gYaw = -90.0f;
static float gPitch = 10.0f;
static float gDist = 14.0f;

static bool gFirstMouse = true;
static double gLastX = SCR_WIDTH / 2.0;
static double gLastY = SCR_HEIGHT / 2.0;

static void framebuffer_size_callback(GLFWwindow*, int width, int height)
{
    glViewport(0, 0, std::max(1, width), std::max(1, height));
}

static void mouse_callback(GLFWwindow* window, double xposIn, double yposIn)
{
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)
    {
        gFirstMouse = true;
        return;
    }

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (gFirstMouse)
    {
        gLastX = xpos;
        gLastY = ypos;
        gFirstMouse = false;
    }

    float xoffset = xpos - static_cast<float>(gLastX);
    float yoffset = static_cast<float>(gLastY) - ypos;

    gLastX = xpos;
    gLastY = ypos;

    const float sensitivity = 0.25f;

    gYaw += xoffset * sensitivity;
    gPitch = std::clamp(gPitch + yoffset * sensitivity, -60.0f, 75.0f);
}

static void scroll_callback(GLFWwindow*, double, double yoffset)
{
    gDist = std::clamp(gDist - static_cast<float>(yoffset) * 0.8f, 1.5f, 50.0f);
}

static glm::mat4 aiToGlm(const aiMatrix4x4& m)
{
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

// BUSQUEDA DE MODELOS GLTF / GLB
static fs::path resolveModelFile(const fs::path& baseDir)
{
    std::error_code ec;

    if (!fs::exists(baseDir, ec))
        return baseDir / "scene.gltf";

    fs::path directGltf = baseDir / "scene.gltf";

    if (fs::exists(directGltf, ec))
        return directGltf;

    fs::path directGlb = baseDir / "scene.glb";

    if (fs::exists(directGlb, ec))
        return directGlb;

    fs::recursive_directory_iterator it(baseDir, ec);
    fs::recursive_directory_iterator end;

    while (!ec && it != end)
    {
        if (it->is_regular_file(ec))
        {
            std::string ext = it->path().extension().string();

            std::transform(
                ext.begin(),
                ext.end(),
                ext.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                }
            );

            if (ext == ".gltf" || ext == ".glb")
                return it->path();
        }

        it.increment(ec);
    }

    return directGltf;
}

// VERTEX SHADER
static const char* kMeshVertShader = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in ivec4 aBoneIds;
layout (location = 4) in vec4 aWeights;

const int MAX_BONES = 128;

uniform mat4 uBones[MAX_BONES];
uniform int uUseSkinning;

uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main()
{
    vec4 localPos = vec4(aPos, 1.0);
    vec3 localNorm = aNormal;

    if (uUseSkinning == 1)
    {
        mat4 skinMat = mat4(0.0);
        float totalW = 0.0;

        for (int i = 0; i < 4; ++i)
        {
            int id = aBoneIds[i];
            float w = aWeights[i];

            if (id >= 0 && id < MAX_BONES && w > 0.0)
            {
                skinMat += uBones[id] * w;
                totalW += w;
            }
        }

        if (totalW > 0.0001)
        {
            localPos = skinMat * localPos;
            localNorm = mat3(skinMat) * localNorm;
        }
    }

    vec4 worldPos = uModel * localPos;

    vWorldPos = worldPos.xyz;
    vNormal = normalize(uNormalMat * localNorm);
    vTexCoord = aTexCoord;

    gl_Position = uProj * uView * worldPos;
}
)GLSL";

// FRAGMENT SHADER
static const char* kMeshFragShader = R"GLSL(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uDiffuseMap;
uniform int uHasTexture;
uniform int uRawUV;
uniform vec3 uBaseColor;
uniform vec3 uLightPos;
uniform vec3 uViewPos;

out vec4 FragColor;

void main()
{
    vec3 albedo = uBaseColor;

    if (uHasTexture == 1)
    {
        vec2 uv = (uRawUV == 1) ? vTexCoord : fract(vTexCoord);
        vec3 texCol = texture(uDiffuseMap, uv).rgb;

        albedo = texCol * (length(uBaseColor) > 0.05 ? uBaseColor : vec3(1.0));
    }

    if (length(albedo) < 0.04)
        albedo = vec3(0.75, 0.75, 0.78);

    vec3 N = normalize(vNormal);

    if (length(N) < 0.001)
        N = vec3(0.0, 1.0, 0.0);

    vec3 L = normalize(uLightPos - vWorldPos);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(abs(dot(N, L)), 0.35);
    float spec = pow(max(dot(N, H), 0.0), 24.0) * 0.25;

    vec3 ambient = 0.42 * albedo;
    vec3 color = ambient + albedo * diff + vec3(spec);

    FragColor = vec4(color, 1.0);
}
)GLSL";

// SHADERS DE LA CURVA
static const char* kLineVertShader = R"GLSL(
#version 330 core

layout (location = 0) in vec3 aPos;

uniform mat4 uView;
uniform mat4 uProj;

void main()
{
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

static const char* kLineFragShader = R"GLSL(
#version 330 core

uniform vec3 uColor;
out vec4 FragColor;

void main()
{
    FragColor = vec4(uColor, 1.0);
}
)GLSL";

// CLASE DE SHADERS
class ShaderProgram
{
public:
    unsigned int id = 0;

    ShaderProgram(const char* vsSrc, const char* fsSrc)
    {
        unsigned int vs = compileStage(GL_VERTEX_SHADER, vsSrc);
        unsigned int fs = compileStage(GL_FRAGMENT_SHADER, fsSrc);

        id = glCreateProgram();

        glAttachShader(id, vs);
        glAttachShader(id, fs);
        glLinkProgram(id);

        int ok = 0;
        glGetProgramiv(id, GL_LINK_STATUS, &ok);

        if (!ok)
        {
            char log[1024];
            glGetProgramInfoLog(id, 1024, nullptr, log);
            std::cerr << "[Shader Link Error] " << log << "\n";
        }

        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    ~ShaderProgram()
    {
        if (id != 0)
            glDeleteProgram(id);
    }

    void use() const
    {
        glUseProgram(id);
    }

    void setMat4(const char* name, const glm::mat4& m) const
    {
        glUniformMatrix4fv(
            glGetUniformLocation(id, name),
            1,
            GL_FALSE,
            glm::value_ptr(m)
        );
    }

    void setMat3(const char* name, const glm::mat3& m) const
    {
        glUniformMatrix3fv(
            glGetUniformLocation(id, name),
            1,
            GL_FALSE,
            glm::value_ptr(m)
        );
    }

    void setVec3(const char* name, const glm::vec3& v) const
    {
        glUniform3fv(
            glGetUniformLocation(id, name),
            1,
            glm::value_ptr(v)
        );
    }

    void setInt(const char* name, int v) const
    {
        glUniform1i(glGetUniformLocation(id, name), v);
    }

private:

    static unsigned int compileStage(GLenum type, const char* src)
    {
        unsigned int s = glCreateShader(type);

        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);

        int ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

        if (!ok)
        {
            char log[1024];
            glGetShaderInfoLog(s, 1024, nullptr, log);
            std::cerr << "[Shader Compile Error] " << log << "\n";
        }

        return s;
    }
};

// CURVA DE BEZIER (un tramo cubico)
struct BezierCurve
{
    glm::vec3 p0{ 0.0f }, p1{ 0.0f }, p2{ 0.0f }, p3{ 0.0f };

    glm::vec3 pointAt(float t) const
    {
        t = std::clamp(t, 0.0f, 1.0f);

        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;

        return (uu * u) * p0
            + (3.0f * uu * t) * p1
            + (3.0f * u * tt) * p2
            + (tt * t) * p3;
    }

    glm::vec3 tangentAt(float t) const
    {
        t = std::clamp(t, 0.0f, 1.0f);

        float u = 1.0f - t;

        glm::vec3 d =
            3.0f * u * u * (p1 - p0)
            + 6.0f * u * t * (p2 - p1)
            + 3.0f * t * t * (p3 - p2);

        if (glm::length(d) < 1e-5f)
            return glm::vec3(1.0f, 0.0f, 0.0f);

        return glm::normalize(d);
    }
};

// CIRCULO FORMADO POR 4 CURVAS DE BEZIER
struct BezierLoop
{
    BezierCurve seg[4];

    BezierLoop(const glm::vec3& center, float r)
    {
        const float k = 0.5522847498f; // constante para aproximar un circulo con Bezier cubica

        for (int i = 0; i < 4; ++i)
        {
            float a0 = glm::radians(90.0f * i);
            float a1 = glm::radians(90.0f * (i + 1));

            glm::vec3 P0 = center + r * glm::vec3(std::cos(a0), 0.0f, std::sin(a0));
            glm::vec3 P3 = center + r * glm::vec3(std::cos(a1), 0.0f, std::sin(a1));
            glm::vec3 d0(-std::sin(a0), 0.0f, std::cos(a0));
            glm::vec3 d1(-std::sin(a1), 0.0f, std::cos(a1));

            seg[i].p0 = P0;
            seg[i].p1 = P0 + k * r * d0;
            seg[i].p2 = P3 - k * r * d1;
            seg[i].p3 = P3;
        }
    }

    // t en [0,1): una vuelta completa
    glm::vec3 pointAt(float t) const
    {
        float s = t * 4.0f;
        int i = std::clamp(static_cast<int>(s), 0, 3);
        return seg[i].pointAt(s - i);
    }

    glm::vec3 tangentAt(float t) const
    {
        float s = t * 4.0f;
        int i = std::clamp(static_cast<int>(s), 0, 3);
        return seg[i].tangentAt(s - i);
    }

    // La cabeza del modelo (+X local) queda alineada con la tangente
    glm::mat4 orientationMatrixXForward(float t) const
    {
        glm::vec3 fwd = tangentAt(t);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 z = glm::normalize(glm::cross(fwd, up));
        glm::vec3 y = glm::cross(z, fwd);

        glm::mat4 rot(1.0f);
        rot[0] = glm::vec4(fwd, 0.0f);
        rot[1] = glm::vec4(y, 0.0f);
        rot[2] = glm::vec4(z, 0.0f);
        return rot;
    }
};

struct Vertex
{
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    glm::vec2 texCoord{ 0.0f, 0.0f };
    glm::ivec4 boneIds{ -1, -1, -1, -1 };
    glm::vec4 weights{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct MeshPart
{
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;

    unsigned int indexCount = 0;
    unsigned int diffuseTex = 0;

    bool hasTexture = false;

    glm::vec3 baseColor{ 0.85f, 0.85f, 0.88f };
    glm::mat4 nodeLocalMat{ 1.0f };
};

class SkinnedModel
{
public:
    std::vector<MeshPart> meshes;
    std::vector<glm::mat4> boneMatrices;

    bool hasAnimations = false;
    bool loadedOk = false;

    glm::mat4 autoNormMatrix{ 1.0f };
    glm::mat4 uprightFix{ 1.0f };

    // fixDeg = (pitch X, yaw Y, roll Z) en grados
    explicit SkinnedModel(
        const fs::path& path,
        float targetHeight = 2.0f,
        bool flipUV = true,
        glm::vec3 fixDeg = glm::vec3(0.0f)
    )
    {
        boneMatrices.assign(128, glm::mat4(1.0f));
        flipUV_ = flipUV;
        targetHeight_ = targetHeight;
        fixDeg_ = fixDeg;

        loadModel(path);
    }

    ~SkinnedModel()
    {
        for (auto& m : meshes)
        {
            if (m.ebo) glDeleteBuffers(1, &m.ebo);
            if (m.vbo) glDeleteBuffers(1, &m.vbo);
            if (m.vao) glDeleteVertexArrays(1, &m.vao);
        }

        for (auto& kv : texCache_)
        {
            if (kv.second != 0)
                glDeleteTextures(1, &kv.second);
        }
    }

    void setFix(const glm::vec3& fixDeg)
    {
        fixDeg_ = fixDeg;

        if (loadedOk)
            computeNormalization();
    }

    glm::vec3 getFix() const
    {
        return fixDeg_;
    }

    void updateAnimation(float timeSec)
    {
        if (!loadedOk || !scene_ || !hasAnimations || scene_->mNumAnimations == 0)
            return;

        const aiAnimation* anim = scene_->mAnimations[0];

        float ticksPerSecond =
            (anim->mTicksPerSecond != 0.0)
            ? static_cast<float>(anim->mTicksPerSecond)
            : 25.0f;

        float duration = static_cast<float>(anim->mDuration);
        float timeInTicks = timeSec * ticksPerSecond;

        float animTime =
            (duration > 0.0f)
            ? std::fmod(timeInTicks, duration)
            : 0.0f;

        std::fill(
            boneMatrices.begin(),
            boneMatrices.end(),
            glm::mat4(1.0f)
        );

        glm::mat4 globalInv =
            glm::inverse(aiToGlm(scene_->mRootNode->mTransformation));

        readNodeHierarchy(
            anim,
            scene_->mRootNode,
            animTime,
            glm::mat4(1.0f),
            globalInv
        );
    }

    void draw(const ShaderProgram& shader, const glm::mat4& modelMat) const
    {
        if (!loadedOk || meshes.empty())
            return;

        shader.use();

        shader.setInt("uUseSkinning", hasAnimations ? 1 : 0);

        if (hasAnimations)
        {
            glUniformMatrix4fv(
                glGetUniformLocation(shader.id, "uBones"),
                static_cast<GLsizei>(boneMatrices.size()),
                GL_FALSE,
                glm::value_ptr(boneMatrices[0])
            );
        }

        for (const auto& part : meshes)
        {
            glm::mat4 finalModel =
                hasAnimations
                ? (modelMat * autoNormMatrix)
                : (modelMat * autoNormMatrix * part.nodeLocalMat);

            shader.setMat4("uModel", finalModel);
            shader.setMat3(
                "uNormalMat",
                glm::mat3(glm::transpose(glm::inverse(finalModel)))
            );

            shader.setInt("uHasTexture", part.hasTexture ? 1 : 0);
            shader.setVec3("uBaseColor", part.baseColor);

            glActiveTexture(GL_TEXTURE0);

            glBindTexture(
                GL_TEXTURE_2D,
                (part.hasTexture && part.diffuseTex != 0)
                ? part.diffuseTex
                : 0
            );

            shader.setInt("uDiffuseMap", 0);

            glBindVertexArray(part.vao);

            glDrawElements(
                GL_TRIANGLES,
                static_cast<GLsizei>(part.indexCount),
                GL_UNSIGNED_INT,
                nullptr
            );
        }

        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

private:

    Assimp::Importer importer_;
    const aiScene* scene_ = nullptr;

    fs::path modelDir_;

    bool flipUV_ = true;

    float targetHeight_ = 2.0f;
    glm::vec3 fixDeg_{ 0.0f };

    std::vector<std::pair<std::string, glm::mat4>> boneOffsets_;

    std::unordered_map<std::string, unsigned int> texCache_;

    glm::vec3 minB_{ std::numeric_limits<float>::max() };
    glm::vec3 maxB_{ std::numeric_limits<float>::lowest() };

    static glm::mat4 makeFix(const glm::vec3& d)
    {
        // Orden de aplicacion: primero Z, luego Y y al final X.
        // Asi, X gira el modelo alrededor del eje de avance (la cabeza en +X).
        glm::mat4 m(1.0f);
        m = glm::rotate(m, glm::radians(d.x), glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, glm::radians(d.y), glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, glm::radians(d.z), glm::vec3(0.0f, 0.0f, 1.0f));
        return m;
    }

    // CALCULAR DIMENSIONES CON LA ORIENTACION CORREGIDA,
    // CENTRAR EL MODELO Y APOYARLO EN Y = 0
    void computeNormalization()
    {
        uprightFix = makeFix(fixDeg_);

        glm::vec3 rotMin(std::numeric_limits<float>::max());
        glm::vec3 rotMax(std::numeric_limits<float>::lowest());

        glm::vec3 corners[8] =
        {
            { minB_.x, minB_.y, minB_.z },
            { maxB_.x, minB_.y, minB_.z },
            { minB_.x, maxB_.y, minB_.z },
            { maxB_.x, maxB_.y, minB_.z },
            { minB_.x, minB_.y, maxB_.z },
            { maxB_.x, minB_.y, maxB_.z },
            { minB_.x, maxB_.y, maxB_.z },
            { maxB_.x, maxB_.y, maxB_.z }
        };

        for (const auto& c : corners)
        {
            glm::vec3 rc =
                glm::vec3(uprightFix * glm::vec4(c, 1.0f));

            rotMin = glm::min(rotMin, rc);
            rotMax = glm::max(rotMax, rc);
        }

        float height = rotMax.y - rotMin.y;

        float maxDim = std::max({
            rotMax.x - rotMin.x,
            height,
            rotMax.z - rotMin.z
            });

        float denom = (height > 1e-4f) ? height : maxDim;

        float scale =
            (denom > 1e-4f)
            ? (targetHeight_ / denom)
            : 1.0f;

        glm::vec3 footCenter(
            (rotMin.x + rotMax.x) * 0.5f,
            rotMin.y,
            (rotMin.z + rotMax.z) * 0.5f
        );

        autoNormMatrix =
            glm::scale(glm::mat4(1.0f), glm::vec3(scale))
            * glm::translate(glm::mat4(1.0f), -footCenter)
            * uprightFix;
    }

    void loadModel(const fs::path& path)
    {
        modelDir_ = path.parent_path();

        unsigned int flags =
            aiProcess_Triangulate |
            aiProcess_LimitBoneWeights;

        scene_ = importer_.ReadFile(path.string(), flags);

        if (!scene_ ||
            (scene_->mFlags & AI_SCENE_FLAGS_INCOMPLETE) ||
            !scene_->mRootNode)
        {
            std::cerr
                << "[Assimp Error] No se pudo cargar: "
                << path.string()
                << " -> "
                << importer_.GetErrorString()
                << "\n";

            return;
        }

        loadedOk = true;
        hasAnimations = (scene_->mNumAnimations > 0);

        for (unsigned int a = 0; a < scene_->mNumAnimations; ++a)
        {
            const aiAnimation* anim = scene_->mAnimations[a];

            std::cout
                << "[Anim] " << a
                << " | Canales: " << anim->mNumChannels
                << " | Duracion: " << anim->mDuration
                << " | Ticks/seg: " << anim->mTicksPerSecond
                << "\n";
        }

        std::cout
            << "[OK] Cargado: " << path.string()
            << " | Meshes: " << scene_->mNumMeshes
            << " | Anims: " << scene_->mNumAnimations
            << "\n";

        collectMeshesFromNode(
            scene_->mRootNode,
            glm::mat4(1.0f)
        );

        computeNormalization();
    }

    void collectMeshesFromNode(
        const aiNode* node,
        const glm::mat4& parentTransform
    )
    {
        glm::mat4 globalTransform =
            parentTransform * aiToGlm(node->mTransformation);

        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            unsigned int meshIndex = node->mMeshes[i];

            if (meshIndex >= scene_->mNumMeshes)
                continue;

            MeshPart part = processMesh(scene_->mMeshes[meshIndex]);

            part.nodeLocalMat = globalTransform;

            meshes.push_back(part);
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            collectMeshesFromNode(
                node->mChildren[i],
                globalTransform
            );
        }
    }

    int findOrAddBone(
        const std::string& name,
        const glm::mat4& offset
    )
    {
        for (size_t i = 0; i < boneOffsets_.size(); ++i)
        {
            if (boneOffsets_[i].first == name)
                return static_cast<int>(i);
        }

        if (boneOffsets_.size() >= 128)
            return -1;

        int idx = static_cast<int>(boneOffsets_.size());

        boneOffsets_.push_back({ name, offset });

        return idx;
    }

    static void addBoneWeight(
        Vertex& v,
        int boneId,
        float weight
    )
    {
        for (int i = 0; i < 4; ++i)
        {
            if (v.weights[i] == 0.0f)
            {
                v.boneIds[i] = boneId;
                v.weights[i] = weight;
                return;
            }
        }
    }

    MeshPart processMesh(const aiMesh* mesh)
    {
        std::vector<Vertex> vertices(mesh->mNumVertices);

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            glm::vec3 p(
                mesh->mVertices[i].x,
                mesh->mVertices[i].y,
                mesh->mVertices[i].z
            );

            vertices[i].position = p;

            minB_ = glm::min(minB_, p);
            maxB_ = glm::max(maxB_, p);

            if (mesh->HasNormals())
            {
                vertices[i].normal = glm::vec3(
                    mesh->mNormals[i].x,
                    mesh->mNormals[i].y,
                    mesh->mNormals[i].z
                );
            }

            if (mesh->mTextureCoords[0])
            {
                vertices[i].texCoord = glm::vec2(
                    mesh->mTextureCoords[0][i].x,
                    mesh->mTextureCoords[0][i].y
                );
            }
        }

        for (unsigned int b = 0; b < mesh->mNumBones; ++b)
        {
            const aiBone* bone = mesh->mBones[b];

            int boneIdx = findOrAddBone(
                bone->mName.C_Str(),
                aiToGlm(bone->mOffsetMatrix)
            );

            if (boneIdx < 0)
                continue;

            for (unsigned int w = 0; w < bone->mNumWeights; ++w)
            {
                addBoneWeight(
                    vertices[bone->mWeights[w].mVertexId],
                    boneIdx,
                    bone->mWeights[w].mWeight
                );
            }
        }

        std::vector<unsigned int> indices;

        indices.reserve(mesh->mNumFaces * 3);

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];

            for (unsigned int j = 0; j < face.mNumIndices; ++j)
            {
                indices.push_back(face.mIndices[j]);
            }
        }

        MeshPart part;

        part.indexCount =
            static_cast<unsigned int>(indices.size());

        if (mesh->mMaterialIndex < scene_->mNumMaterials)
        {
            const aiMaterial* mat =
                scene_->mMaterials[mesh->mMaterialIndex];

            aiColor4D baseCol(
                1.0f, 1.0f, 1.0f, 1.0f
            );

            if (
                AI_SUCCESS == mat->Get(AI_MATKEY_BASE_COLOR, baseCol) ||
                AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_DIFFUSE, baseCol)
                )
            {
                part.baseColor =
                    glm::vec3(baseCol.r, baseCol.g, baseCol.b);
            }

            aiString texPath;

            if (
                mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS ||
                mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS
                )
            {
                part.diffuseTex = getOrLoadTexture(texPath.C_Str());

                part.hasTexture = (part.diffuseTex != 0);
            }
        }

        glGenVertexArrays(1, &part.vao);
        glGenBuffers(1, &part.vbo);
        glGenBuffers(1, &part.ebo);

        glBindVertexArray(part.vao);

        glBindBuffer(GL_ARRAY_BUFFER, part.vbo);

        glBufferData(
            GL_ARRAY_BUFFER,
            vertices.size() * sizeof(Vertex),
            vertices.data(),
            GL_STATIC_DRAW
        );

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ebo);

        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            indices.size() * sizeof(unsigned int),
            indices.data(),
            GL_STATIC_DRAW
        );

        glEnableVertexAttribArray(0);

        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, position))
        );

        glEnableVertexAttribArray(1);

        glVertexAttribPointer(
            1, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, normal))
        );

        glEnableVertexAttribArray(2);

        glVertexAttribPointer(
            2, 2, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, texCoord))
        );

        glEnableVertexAttribArray(3);

        glVertexAttribIPointer(
            3, 4, GL_INT,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, boneIds))
        );

        glEnableVertexAttribArray(4);

        glVertexAttribPointer(
            4, 4, GL_FLOAT, GL_FALSE,
            sizeof(Vertex),
            reinterpret_cast<void*>(offsetof(Vertex, weights))
        );

        glBindVertexArray(0);

        return part;
    }

    unsigned int getOrLoadTexture(const std::string& rawPath)
    {
        auto it = texCache_.find(rawPath);

        if (it != texCache_.end())
            return it->second;

        stbi_set_flip_vertically_on_load(flipUV_ ? 1 : 0);

        int w = 0, h = 0, ch = 0;

        unsigned char* data = nullptr;

        if (!rawPath.empty() && rawPath[0] == '*')
        {
            int embIdx = std::atoi(rawPath.c_str() + 1);

            if (scene_ &&
                embIdx >= 0 &&
                embIdx < static_cast<int>(scene_->mNumTextures))
            {
                const aiTexture* emb = scene_->mTextures[embIdx];

                if (emb->mHeight == 0)
                {
                    data = stbi_load_from_memory(
                        reinterpret_cast<const unsigned char*>(emb->pcData),
                        static_cast<int>(emb->mWidth),
                        &w,
                        &h,
                        &ch,
                        0
                    );
                }
            }
        }
        else
        {
            fs::path rel(rawPath);

            std::vector<fs::path> candidates;

            if (rel.is_absolute())
            {
                candidates.push_back(rel);
            }
            else
            {
                candidates.push_back(modelDir_ / rel);
                candidates.push_back(modelDir_ / "textures" / rel.filename());
                candidates.push_back(rel);
            }

            for (const auto& c : candidates)
            {
                if (fs::exists(c))
                {
                    data = stbi_load(
                        c.string().c_str(),
                        &w,
                        &h,
                        &ch,
                        0
                    );

                    if (data)
                        break;
                }
            }
        }

        if (!data)
        {
            texCache_[rawPath] = 0;
            return 0;
        }

        GLenum fmt = GL_RGB;

        if (ch == 1)
            fmt = GL_RED;
        else if (ch == 4)
            fmt = GL_RGBA;

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        unsigned int tex = 0;

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            fmt,
            w,
            h,
            0,
            fmt,
            GL_UNSIGNED_BYTE,
            data
        );

        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER,
            GL_LINEAR_MIPMAP_LINEAR
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER,
            GL_LINEAR
        );

        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(data);

        texCache_[rawPath] = tex;

        return tex;
    }

    static const aiNodeAnim* findChannel(
        const aiAnimation* anim,
        const std::string& nodeName
    )
    {
        for (unsigned int i = 0; i < anim->mNumChannels; ++i)
        {
            if (nodeName == anim->mChannels[i]->mNodeName.C_Str())
                return anim->mChannels[i];
        }

        return nullptr;
    }

    static glm::vec3 interpVec3(
        const aiVectorKey* keys,
        unsigned int count,
        float t
    )
    {
        if (count == 0)
            return glm::vec3(0.0f);

        if (count == 1 || t <= static_cast<float>(keys[0].mTime))
        {
            return glm::vec3(
                keys[0].mValue.x,
                keys[0].mValue.y,
                keys[0].mValue.z
            );
        }

        for (unsigned int i = 0; i + 1 < count; ++i)
        {
            float t0 = static_cast<float>(keys[i].mTime);
            float t1 = static_cast<float>(keys[i + 1].mTime);

            if (t <= t1)
            {
                float factor =
                    (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;

                glm::vec3 a(
                    keys[i].mValue.x,
                    keys[i].mValue.y,
                    keys[i].mValue.z
                );

                glm::vec3 b(
                    keys[i + 1].mValue.x,
                    keys[i + 1].mValue.y,
                    keys[i + 1].mValue.z
                );

                return glm::mix(a, b, factor);
            }
        }

        return glm::vec3(
            keys[count - 1].mValue.x,
            keys[count - 1].mValue.y,
            keys[count - 1].mValue.z
        );
    }

    static glm::quat interpQuat(
        const aiQuatKey* keys,
        unsigned int count,
        float t
    )
    {
        if (count == 0)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (count == 1 || t <= static_cast<float>(keys[0].mTime))
        {
            return glm::quat(
                keys[0].mValue.w,
                keys[0].mValue.x,
                keys[0].mValue.y,
                keys[0].mValue.z
            );
        }

        for (unsigned int i = 0; i + 1 < count; ++i)
        {
            float t0 = static_cast<float>(keys[i].mTime);
            float t1 = static_cast<float>(keys[i + 1].mTime);

            if (t <= t1)
            {
                float factor =
                    (t1 > t0) ? ((t - t0) / (t1 - t0)) : 0.0f;

                glm::quat a(
                    keys[i].mValue.w,
                    keys[i].mValue.x,
                    keys[i].mValue.y,
                    keys[i].mValue.z
                );

                glm::quat b(
                    keys[i + 1].mValue.w,
                    keys[i + 1].mValue.x,
                    keys[i + 1].mValue.y,
                    keys[i + 1].mValue.z
                );

                return glm::normalize(glm::slerp(a, b, factor));
            }
        }

        const auto& last = keys[count - 1].mValue;

        return glm::normalize(
            glm::quat(last.w, last.x, last.y, last.z)
        );
    }

    // JERARQUIA Y ANIMACION DE HUESOS
    void readNodeHierarchy(
        const aiAnimation* anim,
        const aiNode* node,
        float animTime,
        const glm::mat4& parentTransform,
        const glm::mat4& globalInv
    )
    {
        glm::mat4 nodeTransform =
            aiToGlm(node->mTransformation);

        const aiNodeAnim* ch =
            findChannel(anim, node->mName.C_Str());

        if (ch)
        {
            aiVector3D basePos, baseScale;
            aiQuaternion baseRot;

            node->mTransformation.Decompose(
                baseScale,
                baseRot,
                basePos
            );

            glm::vec3 pos(
                basePos.x,
                basePos.y,
                basePos.z
            );

            glm::quat rot(
                baseRot.w,
                baseRot.x,
                baseRot.y,
                baseRot.z
            );

            glm::vec3 scl(
                baseScale.x,
                baseScale.y,
                baseScale.z
            );

            if (ch->mNumPositionKeys > 0)
            {
                pos = interpVec3(
                    ch->mPositionKeys,
                    ch->mNumPositionKeys,
                    animTime
                );
            }

            if (ch->mNumRotationKeys > 0)
            {
                rot = interpQuat(
                    ch->mRotationKeys,
                    ch->mNumRotationKeys,
                    animTime
                );
            }

            if (ch->mNumScalingKeys > 0)
            {
                scl = interpVec3(
                    ch->mScalingKeys,
                    ch->mNumScalingKeys,
                    animTime
                );
            }

            glm::mat4 T =
                glm::translate(glm::mat4(1.0f), pos);

            glm::mat4 R =
                glm::mat4_cast(glm::normalize(rot));

            glm::mat4 S =
                glm::scale(glm::mat4(1.0f), scl);

            nodeTransform = T * R * S;
        }

        glm::mat4 globalTransform =
            parentTransform * nodeTransform;

        std::string name(node->mName.C_Str());

        for (size_t i = 0; i < boneOffsets_.size(); ++i)
        {
            if (boneOffsets_[i].first == name)
            {
                boneMatrices[i] =
                    globalInv *
                    globalTransform *
                    boneOffsets_[i].second;
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            readNodeHierarchy(
                anim,
                node->mChildren[i],
                animTime,
                globalTransform,
                globalInv
            );
        }
    }
};

// ========================================
// MAIN
// ========================================

// ========================================
// PISO DE PASTO
// ========================================

static unsigned int loadTexture2D(const fs::path& path)
{
    stbi_set_flip_vertically_on_load(1);

    int w = 0, h = 0, ch = 0;

    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &ch, 0);

    if (!data)
    {
        std::cerr << "[Aviso] No se encontro la textura: " << path.string()
            << " (el piso sera gris liso)\n";
        return 0;
    }

    GLenum fmt = GL_RGB;

    if (ch == 1)
        fmt = GL_RED;
    else if (ch == 4)
        fmt = GL_RGBA;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    unsigned int tex = 0;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    return tex;
}

struct GroundMesh
{
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    unsigned int tex = 0;
};

// Cuadrado en el plano XZ, centrado en el origen, de lado 2 * half.
// uvRepeat = cuantas veces se repite la textura a lo largo de cada lado.
static GroundMesh createGround(float half, float uvRepeat, const fs::path& texPath)
{
    GroundMesh g;

    Vertex v[4];

    v[0].position = glm::vec3(-half, 0.0f, -half); v[0].texCoord = glm::vec2(0.0f, 0.0f);
    v[1].position = glm::vec3(half, 0.0f, -half); v[1].texCoord = glm::vec2(uvRepeat, 0.0f);
    v[2].position = glm::vec3(half, 0.0f, half); v[2].texCoord = glm::vec2(uvRepeat, uvRepeat);
    v[3].position = glm::vec3(-half, 0.0f, half); v[3].texCoord = glm::vec2(0.0f, uvRepeat);

    for (auto& vert : v)
        vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);

    unsigned int idx[6] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &g.vao);
    glGenBuffers(1, &g.vbo);
    glGenBuffers(1, &g.ebo);

    glBindVertexArray(g.vao);

    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, normal)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, texCoord)));

    glBindVertexArray(0);

    g.tex = loadTexture2D(texPath);

    return g;
}

int main()
{
    if (!glfwInit())
    {
        std::cerr << "[Error] No se pudo inicializar GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        SCR_WIDTH,
        SCR_HEIGHT,
        "Warrior + Hell Wolf Bezier",
        nullptr,
        nullptr
    );

    if (!window)
    {
        std::cerr << "[Error] No se pudo crear la ventana GLFW\n";

        glfwTerminate();

        return -1;
    }

    glfwMakeContextCurrent(window);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    if (!gladLoadGLLoader(
        reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        std::cerr << "[Error] No se pudo inicializar GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    {   // Bloque: los recursos de OpenGL se liberan ANTES de destruir el contexto

        ShaderProgram meshShader(kMeshVertShader, kMeshFragShader);
        ShaderProgram lineShader(kLineVertShader, kLineFragShader);

        // CARGAR LOS MODELOS

        fs::path warriorFile = resolveModelFile(WARRIOR_DIR);
        fs::path wolfFile = resolveModelFile(WOLF_DIR);

        // Correccion de orientacion = (pitch X, yaw Y, roll Z) en grados.
        // Guerrero: si queda boca abajo, cambiar -90 por 90.
        // Lobo: punto de partida. Usa las teclas 1, 2 y 3 en la ejecucion
        // (suman 90 grados a pitch, yaw y roll) hasta que la cabeza quede
        // sobre +X y las patas en el suelo. Luego copia los valores
        // impresos en la consola aqui.

        SkinnedModel warrior(
            warriorFile,
            2.4f,
            true,
            glm::vec3(-90.0f, 0.0f, 0.0f)
        );

        SkinnedModel hellWolf(
            wolfFile,
            1.7f,
            true,
            glm::vec3(270.0f, 0.0f, 90.0f) // X = roll sobre el eje de avance, Z = cabeza hacia +X
        );

        // SUELO (gltf). Si el suelo sale parado o inclinado, cambia el glm::vec3 de rotacion
        // (por ejemplo -90 en X si el modelo viene con Z hacia arriba).
        std::unique_ptr<SkinnedModel> groundGltf;

        if (USE_GLTF_GROUND)
        {
            groundGltf = std::make_unique<SkinnedModel>(
                resolveModelFile(GROUND_DIR),
                GROUND_SIZE,
                true,
                glm::vec3(0.0f, 0.0f, 0.0f)
            );
        }

        const bool gltfGroundOk = groundGltf && groundGltf->loadedOk;

        // CIRCULO DE BEZIER ALREDEDOR DEL GUERRERO
        // Pasa por detras y por delante de el.

        const glm::vec3 warriorPos(0.0f, 0.0f, 1.0f);
        const float loopRadius = 4.5f;

        BezierLoop wolfLoop(warriorPos, loopRadius);

        std::vector<glm::vec3> curvePts;

        const int curveSamples = 200;

        curvePts.reserve(curveSamples);

        for (int i = 0; i < curveSamples; ++i)
        {
            float s =
                static_cast<float>(i) /
                static_cast<float>(curveSamples);

            glm::vec3 p = wolfLoop.pointAt(s);
            p.y = 0.02f; // evita z-fighting con los pies

            curvePts.push_back(p);
        }

        // CREAR LINEA DE BEZIER

        unsigned int curveVao = 0;
        unsigned int curveVbo = 0;

        glGenVertexArrays(1, &curveVao);
        glGenBuffers(1, &curveVbo);

        glBindVertexArray(curveVao);

        glBindBuffer(GL_ARRAY_BUFFER, curveVbo);

        glBufferData(
            GL_ARRAY_BUFFER,
            curvePts.size() * sizeof(glm::vec3),
            curvePts.data(),
            GL_STATIC_DRAW
        );

        glEnableVertexAttribArray(0);

        glVertexAttribPointer(
            0, 3, GL_FLOAT, GL_FALSE,
            sizeof(glm::vec3),
            nullptr
        );

        glBindVertexArray(0);

        // PISO DE PASTO (40 x 40 unidades, la textura se repite 10 veces por lado)

        // Piso plano con textura: se usa si no hay gltf de suelo.
        GroundMesh ground;

        if (!gltfGroundOk)
            ground = createGround(20.0f, 8.0f, GROUND_TEX);

        const float circumference = 2.0f * 3.14159265f * loopRadius;
        const float wolfSpeed = 4.0f; // unidades por segundo

        float wolfPhase = 0.0f;

        double prevTime = glfwGetTime();

        bool prevKey[3] = { false, false, false };
        const int keys[3] = { GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3 };

        // BUCLE PRINCIPAL

        while (!glfwWindowShouldClose(window))
        {
            double now = glfwGetTime();

            float dt = static_cast<float>(now - prevTime);

            prevTime = now;

            float currentFrame = static_cast<float>(now);

            glfwPollEvents();

            // TECLAS 1, 2, 3: ajustar orientacion del lobo (+90 grados)
            for (int i = 0; i < 3; ++i)
            {
                bool down = glfwGetKey(window, keys[i]) == GLFW_PRESS;

                if (down && !prevKey[i])
                {
                    glm::vec3 f = hellWolf.getFix();
                    f[i] += 90.0f;
                    hellWolf.setFix(f);

                    std::cout
                        << "Fix lobo = ("
                        << f.x << ", " << f.y << ", " << f.z << ")\n";
                }

                prevKey[i] = down;
            }

            glClearColor(0.10f, 0.11f, 0.14f, 1.0f);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            int w = 1;
            int h = 1;

            glfwGetFramebufferSize(window, &w, &h);

            float aspect =
                static_cast<float>(w) /
                static_cast<float>(std::max(1, h));

            // CAMARA (centrada en el guerrero)

            glm::vec3 target(0.0f, 1.0f, 1.0f);

            float radYaw = glm::radians(gYaw);
            float radPitch = glm::radians(gPitch);

            glm::vec3 camPos = target + glm::vec3(
                gDist * std::cos(radPitch) * std::cos(radYaw),
                gDist * std::sin(radPitch),
                gDist * std::cos(radPitch) * std::sin(radYaw)
            );

            glm::mat4 view = glm::lookAt(
                camPos,
                target,
                glm::vec3(0.0f, 1.0f, 0.0f)
            );

            glm::mat4 proj = glm::perspective(
                glm::radians(45.0f),
                aspect,
                0.1f,
                200.0f
            );

            // DIBUJAR CURVA

            lineShader.use();

            lineShader.setMat4("uView", view);
            lineShader.setMat4("uProj", proj);

            lineShader.setVec3("uColor", glm::vec3(1.0f, 0.45f, 0.1f));

            glBindVertexArray(curveVao);

            glDrawArrays(
                GL_LINE_LOOP,
                0,
                static_cast<GLsizei>(curvePts.size())
            );

            glBindVertexArray(0);

            // ILUMINACION

            meshShader.use();

            meshShader.setMat4("uView", view);
            meshShader.setMat4("uProj", proj);

            meshShader.setVec3("uLightPos", glm::vec3(6.0f, 10.0f, 8.0f));
            meshShader.setVec3("uViewPos", camPos);

            // SUELO: gltf si cargo; si no, el piso de respaldo.

            if (gltfGroundOk)
            {
                meshShader.setInt("uRawUV", 0);

                groundGltf->draw(
                    meshShader,
                    glm::translate(
                        glm::mat4(1.0f),
                        glm::vec3(warriorPos.x, GROUND_Y_OFFSET, warriorPos.z)
                    )
                );
            }
            else
            {
                meshShader.setInt("uUseSkinning", 0);
                meshShader.setInt("uRawUV", 1);

                glm::mat4 groundModel =
                    glm::translate(
                        glm::mat4(1.0f),
                        glm::vec3(warriorPos.x, -0.01f, warriorPos.z)
                    );

                meshShader.setMat4("uModel", groundModel);
                meshShader.setMat3("uNormalMat", glm::mat3(1.0f));
                meshShader.setInt("uHasTexture", ground.tex != 0 ? 1 : 0);
                meshShader.setVec3(
                    "uBaseColor",
                    ground.tex != 0
                    ? glm::vec3(1.0f, 1.0f, 1.0f)
                    : glm::vec3(0.45f, 0.43f, 0.40f)
                );

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ground.tex);
                meshShader.setInt("uDiffuseMap", 0);

                glBindVertexArray(ground.vao);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);
                glBindTexture(GL_TEXTURE_2D, 0);

                meshShader.setInt("uRawUV", 0);
            }

            // GUERRERO
            // Su base se apoya en el plano Y = 0.

            warrior.updateAnimation(currentFrame);

            glm::mat4 warriorMat =
                glm::translate(glm::mat4(1.0f), warriorPos);

            warrior.draw(meshShader, warriorMat);

            // LOBO
            // Animacion interna + recorrido por el circulo de Bezier.

            hellWolf.updateAnimation(currentFrame);

            wolfPhase = std::fmod(
                wolfPhase + dt * wolfSpeed / circumference,
                1.0f
            );

            glm::vec3 wolfPos = wolfLoop.pointAt(wolfPhase);

            glm::mat4 wolfMat =
                glm::translate(glm::mat4(1.0f), wolfPos) *
                wolfLoop.orientationMatrixXForward(wolfPhase);

            hellWolf.draw(meshShader, wolfMat);

            glfwSwapBuffers(window);
        }

        // LIBERAR RECURSOS

        glDeleteBuffers(1, &curveVbo);
        glDeleteVertexArrays(1, &curveVao);

        if (ground.tex) glDeleteTextures(1, &ground.tex);
        glDeleteBuffers(1, &ground.ebo);
        glDeleteBuffers(1, &ground.vbo);
        glDeleteVertexArrays(1, &ground.vao);

    }   // aqui se destruyen warrior, hellWolf y shaders con el contexto aun activo

    glfwDestroyWindow(window);

    glfwTerminate();

    return 0;
}
