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

struct WheelAttachment{
    glm::vec3 axlCenter;
    glm::vec3 axlDirection;
    float scale;
    float spinSign;
};

class Application {
public:
    Application()
        : m_window("Final Project", glm::ivec2(1024, 1024), OpenGLVersion::GL41)
        , m_texture(RESOURCE_ROOT "resources/checkerboard.png")
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

        Mesh ground;

        ground.vertices = {
            // Position             Normal             Texture coordinates
            { {-10.0f, 0.0f, -10.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
            { {-10.0f, 0.0f,  10.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },
            { { 10.0f, 0.0f,  10.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },
            { { 10.0f, 0.0f, -10.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} }
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

            // Any new shaders can be added below in similar fashion.
            // ==> Don't forget to reconfigure CMake when you do!
            //     Visual Studio: PROJECT => Generate Cache for ComputerGraphics
            //     VS Code: ctrl + shift + p => CMake: Configure => enter
            // ....
        } catch (ShaderLoadingException e) {
            std::cerr << e.what() << std::endl;
        }
    }
    ~Application()
    {
        glDeleteVertexArrays(1, &m_lightMarkerVAO);
        glDeleteBuffers(1, &m_bezierVBO);
        glDeleteVertexArrays(1, &m_bezierVAO);
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

            ImGui::End();

            const glm::vec3 worldUp { 0.0f, 1.0f, 0.0f };

            const glm::mat4 roverRotation = glm::rotate(
            glm::mat4(1.0f),
            glm::radians(m_roverYawDegrees),
            worldUp);

            // User-controlled position, applied last.
            const glm::mat4 roverTranslation = glm::translate(
                glm::mat4(1.0f),
                m_roverPosition
            );

            // moving model along bezier curve
            beziert += 0.001f;
            if (bezierCnt == 0) {
                glm::vec3 newPosition = bezier(beziert, bezierC1P0, bezierC1P1, bezierC1P2, bezierC1P3);
                m_roverPosition.x = newPosition.x;
                m_roverPosition.y = newPosition.y;
                m_roverPosition.z = newPosition.z;
            }
            else if (bezierCnt == 1) {
                glm::vec3 newPosition = bezier(beziert, bezierC2P0, bezierC2P1, bezierC2P2, bezierC2P3);
                m_roverPosition.x = newPosition.x;
                m_roverPosition.y = newPosition.y;
                m_roverPosition.z = newPosition.z;
            }
            else if (bezierCnt == 2) {
                glm::vec3 newPosition = bezier(beziert, bezierC3P0, bezierC3P1, bezierC3P2, bezierC3P3);
                m_roverPosition.x = newPosition.x;
                m_roverPosition.y = newPosition.y;
                m_roverPosition.z = newPosition.z;
            }
            if (beziert >= 1.0f) {
                bezierCnt += 1;
                if (bezierCnt == 3) {
                    bezierCnt = 0;
                }
                beziert = 0.0f;
            }

            m_modelMatrix = roverTranslation * roverRotation * m_roverBaseRotation;

            // Clear the screen
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // ...
            glEnable(GL_DEPTH_TEST);

            const glm::mat4 mvpMatrix = m_projectionMatrix * camera.viewMatrix() * m_modelMatrix;
            // Normals should be transformed differently than positions (ignoring translations + dealing with scaling):
            // https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html
            const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(m_modelMatrix));

            for (GPUMesh& mesh : m_meshes) {
                m_defaultShader.bind();
                //sun bindings
                glUniform3fv(m_defaultShader.getUniformLocation("sunDirection"), 1,glm::value_ptr(m_sunDirection));
                glUniform3fv(m_defaultShader.getUniformLocation("sunColor"), 1,glm::value_ptr(m_sunColor));
                glUniform1f(m_defaultShader.getUniformLocation("sunIntensity"), m_sunIntensity);
                glUniform1f(m_defaultShader.getUniformLocation("ambientStreantgh"), m_ambientStreangth);

                //camera pos uniforms
                const glm::vec3 cameraPosition = camera.cameraPos();
                glUniform3fv(m_defaultShader.getUniformLocation("cameraPos"), 1, glm::value_ptr(cameraPosition));

                glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(mvpMatrix));
                //Uncomment this line when you use the modelMatrix (or fragmentPosition)
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"), 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
                glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(normalModelMatrix));
                if (mesh.hasTextureCoords()) {
                    m_texture.bind(GL_TEXTURE0);
                    glUniform1i(m_defaultShader.getUniformLocation("colorMap"), 0);
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_TRUE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), GL_FALSE);
                } else {
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_FALSE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), m_useMaterial);
                }
                mesh.draw(m_defaultShader);
            }

            //ground uniforms
            const glm::mat4 groundMVP = m_projectionMatrix * camera.viewMatrix() * m_groundModelMatrix;
            const glm::mat3 groundNormalMatrix = glm::inverseTranspose(glm::mat3(m_groundModelMatrix));

            m_defaultShader.bind();

            glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"),1, GL_FALSE, glm::value_ptr(groundMVP));

            glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"),1, GL_FALSE, glm::value_ptr(m_groundModelMatrix));

            glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"),1, GL_FALSE, glm::value_ptr(groundNormalMatrix));

            glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"),GL_FALSE);

            glUniform1i(m_defaultShader.getUniformLocation("useMaterial"),GL_TRUE);

            m_groundMesh->draw(m_defaultShader);

            
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

                const glm::mat4 wheelMVP = m_projectionMatrix * camera.viewMatrix() * wheelModelMatrix;
                const glm::mat3 wheelNormalMatrix = glm::inverseTranspose(glm::mat3(wheelModelMatrix));

                glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(wheelMVP));
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"), 1, GL_FALSE, glm::value_ptr(wheelModelMatrix));
                glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(wheelNormalMatrix));
                
                for (GPUMesh& wheelMesh : m_wheelMeshes){
                    wheelMesh.draw(m_defaultShader);
                }
            }


            const glm::vec3 headPivot {0.1884334f,0.7467619f,0.0248775f};
            const glm::vec3 headAxis = glm::normalize(glm::vec3(-0.0664598f, 0.9961538f, -0.0571030f));
            const glm::mat4 headToOrigin = glm::translate(glm::mat4(1.0f), -headPivot);
            const glm::mat4 headRotation = glm::rotate(glm::mat4(1.0f), glm::radians(m_headForwardDegrees + m_headYawDegrees), headAxis);
            const glm::mat4 headBackToOrigin = glm::translate(glm::mat4(1.0f), headPivot);

            const glm::mat4 headModelMatrix = m_modelMatrix * headBackToOrigin * headRotation * headToOrigin;

            const glm::mat4 headMVP = m_projectionMatrix * camera.viewMatrix() * headModelMatrix;
            const glm::mat3 headNormalMatrix  = glm::inverseTranspose(headModelMatrix);

            glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"),1, GL_FALSE, glm::value_ptr(headMVP));

            glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"),1, GL_FALSE, glm::value_ptr(headModelMatrix));

            glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"),1, GL_FALSE, glm::value_ptr(headNormalMatrix));

            glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"),GL_FALSE);

            glUniform1i(m_defaultShader.getUniformLocation("useMaterial"),GL_TRUE);

            for(GPUMesh& headMesh : m_headMeshes){
                headMesh.draw(m_defaultShader);
            }
            
            const glm::vec3 referencePoint { 0.0f, 0.0f, 0.0f };
            const float markerDistance = 1.5f;

            const glm::vec3 markerPosition =
                referencePoint
                + glm::normalize(m_sunDirection) * markerDistance;

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
    float m_sunIntensity {1.0f};
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
    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 30.0f);
    glm::mat4 m_modelMatrix { 1.0f };
};

int main()
{
    Application app;
    app.update();

    return 0;
}
