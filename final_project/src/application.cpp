//#include "Image.h"
#include "mesh.h"
#include "texture.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
// Can't wait for modules to fix this stuff...
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include "camera.h"
#include <glad/glad.h>
// Include glad before glfw3
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <functional>
#include <iostream>
#include <vector>
#include <optional>
#include <cmath>
#include <stdexcept>

struct WheelAttachment{
    glm::vec3 axlCenter;
    glm::vec3 axlDirection;
    float scale;
    float spinSign;
};

struct DrawItem{
    GPUMesh* mesh;
    glm::mat4 modelMatrix;
    Texture* colorTexture;
    bool useMaterial;
    Texture* normalTexture = nullptr;
};

struct SceneryInstance {
    size_t modelIndex;
    glm::mat4 modelMatrix;
};

class Application {
public:
    Application()
        : m_window("Final Project", glm::ivec2(1024, 1024), OpenGLVersion::GL41)
        , m_texture(RESOURCE_ROOT "resources/checkerboard.png")
        , m_groundTexture(RESOURCE_ROOT "resources/Stylized_Sand_001_basecolor.jpg")
        , m_groundNormalTexture(RESOURCE_ROOT "resources/Stylized_Sand_001_normal.jpg")
    {
        m_window.registerKeyCallback([this](int key, int scancode, int action, int mods) {
            if (action == GLFW_PRESS)
                onKeyPressed(key, mods);
            else if (action == GLFW_RELEASE)
                onKeyReleased(key, mods);
        });
        m_window.registerMouseMoveCallback(std::bind(&Application::onMouseMove, this, std::placeholders::_1));
        m_window.registerMouseButtonCallback([this](int button, int action, int mods) {
            if (action == GLFW_PRESS)
                onMouseClicked(button, mods);
            else if (action == GLFW_RELEASE)
                onMouseReleased(button, mods);
        });

        m_meshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/rover_body.obj");
        m_headMeshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/rover_head.obj");
        m_wheelMeshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/wheel.obj");

        setupScenery();

        Mesh ground;

        const float groundHalfSize = 100.0f;
        const float groundRepeats = groundHalfSize;
        ground.vertices = {
            // Position             Normal             Texture coordinates
            { {-groundHalfSize, 0.0f, -groundHalfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
            { {-groundHalfSize, 0.0f,  groundHalfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, groundRepeats} },
            { { groundHalfSize, 0.0f,  groundHalfSize}, {0.0f, 1.0f, 0.0f}, {groundRepeats, groundRepeats} },
            { { groundHalfSize, 0.0f, -groundHalfSize}, {0.0f, 1.0f, 0.0f}, {groundRepeats, 0.0f} }
        };
        ground.triangles = {
            {0, 1, 2},
            {0, 2, 3}
        };

        ground.material.kd = glm::vec3(0.45f, 0.22f, 0.12f);
        ground.material.ks = glm::vec3(0.0f);
        ground.material.shininess = 1.0f;
        ground.material.transparency = 1.0f;

        m_groundMesh.emplace(ground);

        try {
            ShaderBuilder defaultBuilder;
            defaultBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shader_vert.glsl");
            defaultBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shader_frag.glsl");
            m_defaultShader = defaultBuilder.build();

            ShaderBuilder markerBuilder;
            markerBuilder.addStage(GL_VERTEX_SHADER,RESOURCE_ROOT "shaders/light_marker_vert.glsl");
            markerBuilder.addStage(GL_FRAGMENT_SHADER,RESOURCE_ROOT "shaders/light_marker_frag.glsl");
            m_lightMarkerShader = markerBuilder.build();
            glGenVertexArrays(1, &m_lightMarkerVAO);

            ShaderBuilder shadowBuilder;
            shadowBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shadow_vert.glsl");
            shadowBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shadow_frag.glsl");
            m_shadowShader = shadowBuilder.build();

            ShaderBuilder bezierBuilder;
            bezierBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/bezier_vert.glsl");
            bezierBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/bezier_frag.glsl");
            m_bezierShader = bezierBuilder.build();
            bezierGeometry();

            ShaderBuilder skyBuilder;
            skyBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/sky_vert.glsl");
            skyBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/sky_frag.glsl");
            m_skyShader = skyBuilder.build();
            glGenVertexArrays(1, &m_skyVAO);

            // Any new shaders can be added below in similar fashion.
            // ==> Don't forget to reconfigure CMake when you do!
            //     Visual Studio: PROJECT => Generate Cache for ComputerGraphics
            //     VS Code: ctrl + shift + p => CMake: Configure => enter
            // ....
        } catch (ShaderLoadingException e) {
            std::cerr << e.what() << std::endl;
        }

        glGenTextures(1, &m_shadowDepthTexture);
        glBindTexture(GL_TEXTURE_2D, m_shadowDepthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float borderDepth[] {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderDepth);
        glGenFramebuffers(1, &m_shadowFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowDepthTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const GLenum status =  glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Shadow framebuffer is incomplete");
        }

    }
    ~Application()
    {
        glDeleteVertexArrays(1, &m_lightMarkerVAO);
        glDeleteVertexArrays(1, &m_skyVAO);
        glDeleteBuffers(1, &m_bezierVBO);
        glDeleteVertexArrays(1, &m_bezierVAO);
        glDeleteFramebuffers(1, &m_shadowFramebuffer);
        glDeleteTextures(1, &m_shadowDepthTexture);
    }

    void setupScenery()
    {
        const char* files[] = {
            "Rock_Large_1.obj", "Rock_Large_2.obj", "Rock_Large_3.obj", "Rock_4.obj"
        };
        const glm::vec3 colors[] = {
            {0.48f, 0.25f, 0.15f}, {0.58f, 0.32f, 0.20f},
            {0.40f, 0.22f, 0.16f}, {0.64f, 0.39f, 0.24f}
        };

        for (size_t i = 0; i < 4; i++) {
            std::vector<Mesh> meshes = loadMesh(std::filesystem::path(RESOURCE_ROOT "resources") / files[i]);
            glm::vec3 minimum = meshes.at(0).vertices.at(0).position;
            glm::vec3 maximum = minimum;
            for (const Mesh& mesh : meshes) {
                for (const Vertex& vertex : mesh.vertices) {
                    minimum = glm::min(minimum, vertex.position);
                    maximum = glm::max(maximum, vertex.position);
                }
            }

            // Make every asset one unit wide, with its base at y = 0.
            const glm::vec3 baseCenter((minimum.x + maximum.x) * 0.5f,
                minimum.y, (minimum.z + maximum.z) * 0.5f);
            const float width = glm::max(maximum.x - minimum.x, maximum.z - minimum.z);
            m_sceneryMeshes.emplace_back();
            for (Mesh& mesh : meshes) {
                for (Vertex& vertex : mesh.vertices) {
                    vertex.position = (vertex.position - baseCenter) / width;
                }
                mesh.material.kd = colors[i];
                mesh.material.ks = glm::vec3(0.0f);
                mesh.material.kdTexture.reset();
                m_sceneryMeshes.back().emplace_back(mesh);
            }
        }

        // Local rocks stay outside the rover's three-curve route.
        addScenery(0, {-3.0f, -0.62f, 2.0f}, {1.8f, 1.2f, 1.4f}, 25.0f);
        addScenery(3, {-2.0f, -0.62f, 3.2f}, {0.45f, 0.4f, 0.45f}, 80.0f);
        addScenery(2, {3.0f, -0.62f, -4.8f}, {1.1f, 0.8f, 1.0f}, 15.0f);
        addScenery(0, {4.2f, -0.62f, -5.2f}, {0.6f, 0.5f, 0.7f}, 130.0f);
        addScenery(1, {7.0f, -0.62f, 9.0f}, {2.1f, 1.5f, 1.8f}, 65.0f);
        addScenery(0, {9.0f, -0.62f, 8.8f}, {0.9f, 0.7f, 0.8f}, 110.0f);
        addScenery(2, {15.0f, -0.62f, 2.0f}, {1.9f, 1.6f, 1.6f}, 210.0f);
        addScenery(3, {14.8f, -0.62f, -2.5f}, {0.8f, 0.7f, 0.8f}, 40.0f);
        addScenery(0, {6.0f, -0.62f, 1.5f}, {1.6f, 1.1f, 1.3f}, 145.0f);
        addScenery(3, {5.2f, -0.62f, 2.8f}, {0.5f, 0.45f, 0.5f}, 35.0f);

        // Two staggered rings of larger rocks form a distant skyline.
        for (int ring = 0; ring < 2; ring++) {
            const int count = ring == 0 ? 18 : 24;
            const float radius = ring == 0 ? 45.0f : 73.0f;
            for (int i = 0; i < count; i++) {
                const float angle = 360.0f * static_cast<float>(i) / static_cast<float>(count) + 9.0f * static_cast<float>(ring);
                const float radians = glm::radians(angle);
                const float width = (ring == 0 ? 17.0f : 23.0f) + static_cast<float>(i % 3) * 2.0f;
                const float height = (ring == 0 ? 7.0f : 12.0f) + static_cast<float>(i % 4) * 2.0f;
                addScenery(static_cast<size_t>(i % 3),
                    {6.0f + radius * std::cos(radians), -1.0f, radius * std::sin(radians)},
                    {width, height, width * 0.75f}, angle + 37.0f * static_cast<float>(i));
            }
        }
    }

    void addScenery(size_t modelIndex, glm::vec3 position, glm::vec3 scale, float yawDegrees)
    {
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), position)
            * glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f))
            * glm::scale(glm::mat4(1.0f), scale);
        m_sceneryInstances.push_back({modelIndex, model});
    }

    glm::vec3 bezier(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3) {
        return ((1.0f - t) * (1.0f - t) * (1.0f - t)) * P0 + (3.0f * (1.0f - t) * (1.0f - t) * t) * P1 + (3.0f * (1.0f - t) * t * t) * P2 + (t * t * t) * P3;
    }

    void bezierGeometry() {
        // 16 points along each curve
        int segments = 16;
        std::vector<glm::vec3> vertices;

        // points on 1st bezier curve
        for (int i = 0; i <= segments; i++) {
            float t = static_cast<float>(i) / segments;

            glm::vec3 point = bezier(
                t,
                bezierC1P0,
                bezierC1P1,
                bezierC1P2,
                bezierC1P3
            );

            vertices.push_back(point);
        }

        // points on 2nd bezier curve
        for (int i = 0; i <= segments; i++) {
            float t = static_cast<float>(i) / segments;

            glm::vec3 point = bezier(
                t,
                bezierC2P0,
                bezierC2P1,
                bezierC2P2,
                bezierC2P3
            );

            vertices.push_back(point);
        }

        // points on 3rd bezier curve
        for (int i = 0; i <= segments; i++) {
            float t = static_cast<float>(i) / segments;

            glm::vec3 point = bezier(
                t,
                bezierC3P0,
                bezierC3P1,
                bezierC3P2,
                bezierC3P3
            );

            vertices.push_back(point);
        }

        m_bezierVertexCount = static_cast<GLsizei>(vertices.size());

        glGenVertexArrays(1, &m_bezierVAO);
        glGenBuffers(1, &m_bezierVBO);

        glBindVertexArray(m_bezierVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_bezierVBO);

        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(glm::vec3), vertices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void update()
    {
        int dummyInteger = 0; // Initialized to 0
        while (!m_window.shouldClose()) {
            // This is your game loop
            // Put your real-time logic and rendering in here
            m_window.updateInput();
            camera.updateInput();

            // Use ImGui for easy input/output of ints, floats, strings, etc...
            ImGui::Begin("Window");
            ImGui::InputInt("This is an integer input", &dummyInteger); // Use ImGui::DragInt or ImGui::DragFloat for larger range of numbers.
            ImGui::Text("Value is: %i", dummyInteger); // Use C printf formatting rules (%i is a signed integer)
            ImGui::Checkbox("Use material if no texture", &m_useMaterial);

            //angle slider for wheel test
            ImGui::SliderFloat("Wheel angle", &m_wheelAngleDegrees, 0.0f, 360.0f);

            //rover sliders
            ImGui::DragFloat3("Rover Position", glm::value_ptr(m_roverPosition), 0.01f);
            ImGui::SliderFloat("Rover Heading", &m_roverYawDegrees, -180.0f, 180.0f);

            //head slider
            ImGui::SliderFloat("Head Rotation", &m_headYawDegrees, -90.0f, 90.0f);

            // enable drawing of bezier
            ImGui::Checkbox("Draw Bezier curves", &drawCurve);

            //wheel offset
            //ImGui::SliderFloat("Wheel outward offset", &m_wheelOutwardOffset, 0.0f, 0.25f);

            ImGui::SliderFloat(
            "Sun azimuth",
            &m_sunAzimuthDegrees,
            -180.0f,
            180.0f);

            ImGui::SliderFloat(
            "Sun elevation",
            &m_sunElevationDegrees,
            0.0f,
            90.0f);

            ImGui::Checkbox("Point light (at yellow marker)", &m_usePointLight);
            ImGui::SliderFloat("Light / marker distance", &m_sunDistance, 10.0f, 120.0f);
            ImGui::SliderFloat("Light intensity", &m_sunIntensity, 0.0f, 5.0f);
            ImGui::TextUnformatted(m_usePointLight
                ? "Point: direction varies with surface position."
                : "Directional: parallel light rays everywhere.");
            
            ImGui::Checkbox("Shadows", &m_enableShadows);
            ImGui::SliderFloat("Shadow bias",&m_shadowBias,0.0f,0.0002f,"%.6f");

            ImGui::Checkbox("Ground normal mapping", &m_enableNormalMapping);
            ImGui::SliderFloat("Ground normal streangth", &m_normalStrength, 0.0f, 2.0f);

            ImGui::Checkbox("Scenery rocks", &m_showScenery);
            ImGui::Checkbox("Procedural sky", &m_showSky);
            ImGui::Checkbox("Distance haze", &m_enableHaze);

            ImGui::End();

            const float azimuth = glm::radians(m_sunAzimuthDegrees);
            const float elevation = glm::radians(m_sunElevationDegrees);

            m_sunDirection = glm::vec3(
            std::cos(elevation) * std::cos(azimuth),
            std::sin(elevation),
            std::cos(elevation) * std::sin(azimuth));

            // World-space light position, independent of the rover transform.
            // Also used by the marker so it represents the actual point light.
            const glm::vec3 sunPosition =
                glm::normalize(m_sunDirection) * m_sunDistance;

            const glm::vec3 worldUp { 0.0f, 1.0f, 0.0f };

            const glm::mat4 roverRotation = glm::rotate(
            glm::mat4(1.0f),
            glm::radians(m_roverYawDegrees),
            worldUp);

            // moving model along bezier curve
            beziert += 0.001f;
            if (bezierCnt == 0) {
                glm::vec3 newPosition = bezier(beziert, bezierC1P0, bezierC1P1, bezierC1P2, bezierC1P3);
                m_roverPosition.x = newPosition.x;
                m_roverPosition.z = newPosition.z;
            }
            else if (bezierCnt == 1) {
                glm::vec3 newPosition = bezier(beziert, bezierC2P0, bezierC2P1, bezierC2P2, bezierC2P3);
                m_roverPosition.x = newPosition.x;
                m_roverPosition.z = newPosition.z;
            }
            else if (bezierCnt == 2) {
                glm::vec3 newPosition = bezier(beziert, bezierC3P0, bezierC3P1, bezierC3P2, bezierC3P3);
                m_roverPosition.x = newPosition.x;
                m_roverPosition.z = newPosition.z;
            }
            if (beziert >= 1.0f) {
                bezierCnt += 1;
                if (bezierCnt == 3) {
                    bezierCnt = 0;
                }
                beziert = 0.0f;
            }

            // User-controlled position, applied last.
            const glm::mat4 roverTranslation = glm::translate(
                glm::mat4(1.0f),
                m_roverPosition
            );

            m_modelMatrix = roverTranslation * roverRotation * m_roverBaseRotation;

            // Clear the screen
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // ...
            glEnable(GL_DEPTH_TEST);

            if (m_showSky) {
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                m_skyShader.bind();
                const glm::mat4 inverseProjection = glm::inverse(m_projectionMatrix);
                const glm::mat3 inverseViewRotation = glm::transpose(glm::mat3(camera.viewMatrix()));
                glUniformMatrix4fv(m_skyShader.getUniformLocation("inverseProjection"),
                    1, GL_FALSE, glm::value_ptr(inverseProjection));
                glUniformMatrix3fv(m_skyShader.getUniformLocation("inverseViewRotation"),
                    1, GL_FALSE, glm::value_ptr(inverseViewRotation));
                glUniform3fv(m_skyShader.getUniformLocation("horizonColor"), 1, glm::value_ptr(m_horizonColor));
                glUniform3fv(m_skyShader.getUniformLocation("zenithColor"), 1, glm::value_ptr(m_zenithColor));
                glBindVertexArray(m_skyVAO);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
                glDepthMask(GL_TRUE);
                glEnable(GL_DEPTH_TEST);
            }

            std::vector<DrawItem> drawItems;
            if (m_showScenery) {
                for (const SceneryInstance& scenery : m_sceneryInstances) {
                    for (GPUMesh& mesh : m_sceneryMeshes[scenery.modelIndex]) {
                        drawItems.push_back({&mesh, scenery.modelMatrix, nullptr, true});
                    }
                }
            }

            const glm::mat4 mvpMatrix = m_projectionMatrix * camera.viewMatrix() * m_modelMatrix;
            // Normals should be transformed differently than positions (ignoring translations + dealing with scaling):
            // https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html
            const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(m_modelMatrix));

            for (GPUMesh& mesh : m_meshes) {
                Texture* colorTexture =
                    mesh.hasTextureCoords() ? &m_texture : nullptr;

                drawItems.push_back({
                    &mesh,
                    m_modelMatrix,
                    colorTexture,
                    m_useMaterial
                });
            }


            drawItems.push_back(
                {
                    &m_groundMesh.value(), m_groundModelMatrix, &m_groundTexture, true, &m_groundNormalTexture
                }
            );

            
            const glm::vec3 wheelCenter = {-0.00035345f, 0.00242475f, 0.00189170f};
            const glm::vec3 wheelLocalAxis = {1.0f, 0.0f, 0.0f};
            const glm::mat4 wheelCentering = glm::translate(glm::mat4(1.0f), -wheelCenter);

            m_defaultShader.bind();

            glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_FALSE);
            glUniform1i(m_defaultShader.getUniformLocation("useMaterial"),GL_TRUE);

            for (const WheelAttachment& wheel : m_wheelAttachments )
            {
                //we need to turn the wheel to match the axis of the robot's axle - kind of difficult calculations
                //we need these to rotate the wheel accurately to the axle of the rover
                const glm::vec3 axleDirection = glm::normalize(wheel.axlDirection);
                const glm::vec3 alignmentAxis = glm::normalize(glm::cross(wheelLocalAxis, axleDirection));
                const float alignmentAngle = glm::acos(glm::clamp(glm::dot(wheelLocalAxis, axleDirection), -1.0f, 1.0f));
                const glm::mat4 wheelAlignment = glm::rotate(glm::mat4(1.0f), alignmentAngle, alignmentAxis);
                
                const glm::vec3 attachmentPosition = wheel.axlCenter + m_wheelOutwardOffset * axleDirection;
                const glm::mat4 wheelAttachment = glm::translate(glm::mat4(1.0f), attachmentPosition);

                const glm::mat4 wheelScale = glm::scale(glm::mat4(1.0f), glm::vec3(wheel.scale));

                const glm::mat4 wheelRotation = glm::rotate(glm::mat4(1.0f), glm::radians(m_wheelAngleDegrees * wheel.spinSign), wheelLocalAxis);                

                const glm::mat4 wheelModelMatrix = m_modelMatrix *
                                                wheelAttachment *
                                                wheelAlignment *
                                                wheelScale *
                                                wheelRotation *
                                                wheelCentering;

                for (GPUMesh& wheelMesh : m_wheelMeshes){
                        drawItems.push_back({
                        &wheelMesh,
                        wheelModelMatrix,
                        nullptr,
                        true
                    });
                }
            }


            const glm::vec3 headPivot {0.1884334f,0.7467619f,0.0248775f};
            const glm::vec3 headAxis = glm::normalize(glm::vec3(-0.0664598f, 0.9961538f, -0.0571030f));
            const glm::mat4 headToOrigin = glm::translate(glm::mat4(1.0f), -headPivot);
            const glm::mat4 headRotation = glm::rotate(glm::mat4(1.0f), glm::radians(m_headForwardDegrees + m_headYawDegrees), headAxis);
            const glm::mat4 headBackToOrigin = glm::translate(glm::mat4(1.0f), headPivot);

            const glm::mat4 headModelMatrix = m_modelMatrix * headBackToOrigin * headRotation * headToOrigin;

           for (GPUMesh& headMesh : m_headMeshes) {
                drawItems.push_back({
                    &headMesh,
                    headModelMatrix,
                    nullptr,
                    true
                });
            }

            m_defaultShader.bind();

            const glm::vec3 cameraPosition = camera.cameraPos();
            const glm::vec3 lightTarget { 6.0f, -0.595f, 1.0f };
            glm::vec3 lightEye = sunPosition;
            if (!m_usePointLight) {
                lightEye = lightTarget + glm::normalize(m_sunDirection) * 100.0f;
            }
            const glm::vec3 lightForward = glm::normalize(lightTarget - lightEye);
            glm::vec3 lightUp = { 0.0f, 1.0f, 0.0f };
            if (std::abs(glm::dot(lightForward, lightUp)) > 0.99f) {
                lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            const glm::mat4 lightView = glm::lookAt(lightEye, lightTarget, lightUp);
            glm::mat4 lightProjection;
            if (m_usePointLight) {
            lightProjection = glm::perspective(glm::radians(70.0f), 1.0f, 1.0f, 200.0f);
            } else {
            lightProjection = glm::ortho(-24.0f, 24.0f, -24.0f, 24.0f, 1.0f, 200.0f);
}
            const glm::mat4 lightViewProjection = lightProjection * lightView; 

            
            if(m_enableShadows)
            {
                GLint previousViewport[4];
                glGetIntegerv(GL_VIEWPORT, previousViewport);
                
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0);

                glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
                glViewport(0,0,SHADOW_SIZE, SHADOW_SIZE);

                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
                glClearDepth(1.0);
                glClear(GL_DEPTH_BUFFER_BIT);

                m_shadowShader.bind();

                for(const DrawItem& item: drawItems)
                {
                    const glm::mat4 lightMVP = lightViewProjection * item.modelMatrix;
                    glUniformMatrix4fv(m_shadowShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(lightMVP));

                    item.mesh->drawGeometry();
                }

                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport( previousViewport[0],  previousViewport[1],  previousViewport[2],  previousViewport[3]);
            }
            
            m_defaultShader.bind();

            glUniform1i(
            m_defaultShader.getUniformLocation("enableShadows"), m_enableShadows);
            glUniform1f(m_defaultShader.getUniformLocation("shadowBias"),m_shadowBias);

            glUniformMatrix4fv(m_defaultShader.getUniformLocation("lightViewProjection"),1,GL_FALSE,glm::value_ptr(lightViewProjection));

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_shadowDepthTexture);

            glUniform1i(m_defaultShader.getUniformLocation("shadowMap"),1);

            glActiveTexture(GL_TEXTURE0);  

            glUniform3fv(
                m_defaultShader.getUniformLocation("cameraPos"),
                1,
                glm::value_ptr(cameraPosition));

            glUniform1i(
                m_defaultShader.getUniformLocation("usePointLight"),
                m_usePointLight);

            glUniform3fv(
                m_defaultShader.getUniformLocation("sunPosition"),
                1,
                glm::value_ptr(sunPosition));

            glUniform3fv(
                m_defaultShader.getUniformLocation("sunDirection"),
                1,
                glm::value_ptr(m_sunDirection));

            glUniform3fv(
                m_defaultShader.getUniformLocation("sunColor"),
                1,
                glm::value_ptr(m_sunColor));

            glUniform1f(
                m_defaultShader.getUniformLocation("sunIntensity"),
                m_sunIntensity);

            glUniform1f(
                m_defaultShader.getUniformLocation("ambientStreantgh"),
                m_ambientStreangth);

            glUniform1i(
                m_defaultShader.getUniformLocation("colorMap"),
                0);

            glUniform1i(m_defaultShader.getUniformLocation("enableHaze"), m_enableHaze && m_showSky);
            glUniform3fv(m_defaultShader.getUniformLocation("horizonColor"), 1, glm::value_ptr(m_horizonColor));
            glUniform3fv(m_defaultShader.getUniformLocation("zenithColor"), 1, glm::value_ptr(m_zenithColor));

            // Keep both samplers valid even when a material has no texture.
            m_groundNormalTexture.bind(GL_TEXTURE2);
            m_texture.bind(GL_TEXTURE0);

            const glm::mat4 cameraViewProjection =
                m_projectionMatrix * camera.viewMatrix();

            for (const DrawItem& item : drawItems) {
                const glm::mat4 itemMVP =
                    cameraViewProjection * item.modelMatrix;

                const glm::mat3 itemNormalMatrix =
                    glm::inverseTranspose(
                        glm::mat3(item.modelMatrix));

                glUniformMatrix4fv(
                    m_defaultShader.getUniformLocation("mvpMatrix"),
                    1,
                    GL_FALSE,
                    glm::value_ptr(itemMVP));

                glUniformMatrix4fv(
                    m_defaultShader.getUniformLocation("modelMatrix"),
                    1,
                    GL_FALSE,
                    glm::value_ptr(item.modelMatrix));

                glUniformMatrix3fv(
                    m_defaultShader.getUniformLocation(
                        "normalModelMatrix"),
                    1,
                    GL_FALSE,
                    glm::value_ptr(itemNormalMatrix));

                const bool hasTexture =
                    item.colorTexture != nullptr;

                if (hasTexture) {
                    item.colorTexture->bind(GL_TEXTURE0);
                }

                glUniform1i(
                    m_defaultShader.getUniformLocation(
                        "hasTexCoords"),
                    hasTexture);

                glUniform1i(
                    m_defaultShader.getUniformLocation(
                        "useMaterial"),
                    hasTexture ? GL_FALSE : item.useMaterial);

                glUniform1i(
                m_defaultShader.getUniformLocation("useMaterial"),
                hasTexture ? GL_FALSE : item.useMaterial);

                const bool useNormalMap = m_enableNormalMapping && item.normalTexture != nullptr;
                glUniform1i(m_defaultShader.getUniformLocation("useNormalMap"), useNormalMap);
                glUniform1i(m_defaultShader.getUniformLocation("normalMap"), 2);
                glUniform1f(m_defaultShader.getUniformLocation("normalStreangth"), m_normalStrength);
                if(useNormalMap)
                {
                    item.normalTexture->bind(GL_TEXTURE2);
                }
                glActiveTexture(GL_TEXTURE0);

                item.mesh->draw(m_defaultShader);
                
            }
            
            const glm::vec3 markerPosition = sunPosition;

            const glm::mat4 viewProjection =
                m_projectionMatrix * camera.viewMatrix();

            m_lightMarkerShader.bind();

            glUniformMatrix4fv(
                m_lightMarkerShader.getUniformLocation("viewProjection"),
                1, GL_FALSE, glm::value_ptr(viewProjection));

            glUniform3fv(
                m_lightMarkerShader.getUniformLocation("markerPosition"),
                1, glm::value_ptr(markerPosition));

            glPointSize(16.0f);
            glBindVertexArray(m_lightMarkerVAO);
            glDrawArrays(GL_POINTS, 0, 1);
            glBindVertexArray(0);
            glPointSize(1.0f);

            if (drawCurve == true) {
                m_bezierShader.bind();
                glUniformMatrix4fv(m_bezierShader.getUniformLocation("viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
                glUniform3f(m_bezierShader.getUniformLocation("lineColor"), 1.0f, 0.8f, 0.0f);
                glBindVertexArray(m_bezierVAO);
                glDrawArrays(GL_LINE_STRIP, 0, m_bezierVertexCount);
                glBindVertexArray(0);
            }

            // Processes input and swaps the window buffer
            m_window.swapBuffers();
        }
    }

    // In here you can handle key presses
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyPressed(int key, int mods)
    {
        switch (key) {
            case GLFW_KEY_1:
                // if we are currently in cam 2
                if (!cam1Selected) {
                    // switch cam to cam 1's saved location
                    oldCam1.setUserInteraction(true);
                    camera = oldCam1;

                    cam1Selected = true;
                }
                break;
            case GLFW_KEY_2:
                // opposite to code above ^
                if (cam1Selected) {
                    oldCam1 = camera;
                    oldCam1.setUserInteraction(false);

                    oldCam2.setUserInteraction(false);
                    camera = oldCam2;

                    cam1Selected = false;
                }
                break;
            default:
                break;
        }
    }

    // In here you can handle key releases
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyReleased(int key, int mods)
    {
        std::cout << "Key released: " << key << std::endl;
    }

    // If the mouse is moved this function will be called with the x, y screen-coordinates of the mouse
    void onMouseMove(const glm::dvec2& cursorPos)
    {
        std::cout << "Mouse at position: " << cursorPos.x << " " << cursorPos.y << std::endl;
    }

    // If one of the mouse buttons is pressed this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseClicked(int button, int mods)
    {
        std::cout << "Pressed mouse button: " << button << std::endl;
    }

    // If one of the mouse buttons is released this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseReleased(int button, int mods)
    {
        std::cout << "Released mouse button: " << button << std::endl;
    }

private:
    Window m_window;
    // free-moving cam
    Camera camera{ &m_window, glm::vec3(1.2f, 1.1f, 0.9f), -glm::vec3(1.2f, 1.1f, 0.9f) };
    Camera oldCam1{ &m_window, glm::vec3(1.2f, 1.1f, 0.9f), -glm::vec3(1.2f, 1.1f, 0.9f) };
    
    // top-down cam (immovable)
    Camera oldCam2{ &m_window, glm::vec3(0.0f, 14.0f, 0.5f), -glm::vec3(0.0f, 14.0f, 0.5f) };
    bool cam1Selected = true;

    //sun's direction from the surface to its position
    glm::vec3 m_sunDirection { 1.0f, 1.0f, 0.5f};
    glm::vec3 m_sunColor { 1.0f, 0.95f, 0.85f };
    float m_sunIntensity { 0.9f };
    bool m_usePointLight { true };
    float m_sunDistance { 60.0f };
    float m_ambientStreangth {0.15f};

    // bezier values - curve 1
    glm::vec3 bezierC1P0 = { 0.0f, 0.0f, 0.0f };
    glm::vec3 bezierC1P1 = { 0.0f, 0.0f, 4.0f };
    glm::vec3 bezierC1P2 = { 2.0f, 0.0f, 6.0f };
    glm::vec3 bezierC1P3 = { 6.0f, 0.0f, 6.0f };
    float beziert = 0.0f;
    int bezierCnt = 0;

    // bezier values - curve 2
    glm::vec3 bezierC2P0 = { 6.0f, 0.0f, 6.0f };
    glm::vec3 bezierC2P1 = { 10.0f, 0.0f, 6.0f };
    glm::vec3 bezierC2P2 = { 12.0f, 0.0f, 4.0f };
    glm::vec3 bezierC2P3 = { 12.0f, 0.0f, 0.0f };

    // bezier values - curve 3
    glm::vec3 bezierC3P0 = { 12.0f, 0.0f, 0.0f };
    glm::vec3 bezierC3P1 = { 12.0f, 0.0f, -4.0f };
    glm::vec3 bezierC3P2 = { 0.0f, 0.0f, -4.0f };
    glm::vec3 bezierC3P3 = { 0.0f, 0.0f, 0.0f };

    // bezier values - drawing the curves
    Shader m_bezierShader;
    GLuint m_bezierVAO{ 0 };
    GLuint m_bezierVBO{ 0 };
    GLsizei m_bezierVertexCount{ 0 };
    bool drawCurve = false;

    float m_sunAzimuthDegrees { 26.565f };
    float m_sunElevationDegrees { 20.0f };
    
    // Shader for default rendering and for depth rendering
    Shader m_defaultShader;
    Shader m_shadowShader;

    Shader m_lightMarkerShader;
    GLuint m_lightMarkerVAO { 0 };

    std::vector<GPUMesh> m_meshes;
    Texture m_texture;
    bool m_useMaterial { true };

    std::vector<GPUMesh> m_wheelMeshes;
    float m_wheelAngleDegrees { 0.0f };

    std::vector<GPUMesh> m_headMeshes;
    float m_headYawDegrees {0.0f};
    float m_headForwardDegrees {-36.22f};

    // negative z for right side
    std::vector<WheelAttachment> m_wheelAttachments 
    {
        {
            {-0.53679944f, -0.50378138f, -0.56606229f},
            {-0.00838811f, -0.01739521f, -0.99981351f},
            0.287f,
            1.0f
        },
        {
            {-0.10203267f, -0.39659327f, -0.62283970f},
            {-0.01852402f, -0.04827393f, -0.99866235f},
            0.340f,
            1.0f
        },
        {
            {0.30974216f, -0.35709362f, -0.69497362f},
            {-0.19380450f, -0.05084552f, -0.97972167f},
            0.295f,
            1.0f
        },

        // positive z for left side
        {
            {-0.33616856f, -0.45795566f, 0.69350196f},
            {0.31860925f, 0.02903868f, 0.94744124f},
            0.287f,
            -1.0f
        },
        {
            {0.09302840f, -0.35404204f, 0.60008422f},
            {0.30873124f, -0.00172264f, 0.95114776f},
            0.340f,
            -1.0f
        },
        {
            {0.50568222f, -0.31672174f, 0.53192258f},
            {0.13688262f, -0.00283869f, 0.99058321f},
            0.295f,
            -1.0f
        }
    };

    std::vector<std::vector<GPUMesh>> m_sceneryMeshes;
    std::vector<SceneryInstance> m_sceneryInstances;
    bool m_showScenery { true };
    bool m_showSky { true };
    bool m_enableHaze { true };
    Shader m_skyShader;
    GLuint m_skyVAO { 0 };
    glm::vec3 m_horizonColor {0.86f, 0.65f, 0.46f};
    glm::vec3 m_zenithColor {0.42f, 0.24f, 0.19f};

    Texture m_groundTexture;
    Texture m_groundNormalTexture;
    bool m_enableNormalMapping { true };
    float m_normalStrength{ 1.0f };

    glm::vec3 m_roverPosition {0.0f, -0.035f, 0.0f};
    glm::mat4 m_roverBaseRotation = glm::rotate(
            glm::mat4(1.0f),
            glm::radians(9.8051f),
            glm::vec3(0.0416051f, 0.0f, -0.999134f));
    float m_roverYawDegrees {0.0f};
    float m_wheelOutwardOffset {0.1116f};

    std::optional<GPUMesh> m_groundMesh;
    glm::mat4 m_groundModelMatrix = glm::translate(glm::mat4(1.0f),glm::vec3(0.0f,-0.595f, 0.0f));

    // Projection and view matrices for you to fill in and use
    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 300.0f);
    glm::mat4 m_modelMatrix { 1.0f };

    //needed for shadow 
    static constexpr int SHADOW_SIZE = 2048;
    GLuint m_shadowDepthTexture { 0 };
    GLuint m_shadowFramebuffer { 0 };
    bool m_enableShadows {true};
    float m_shadowBias { 0.00002f };
};

int main()
{
    Application app;
    app.update();

    return 0;
}
