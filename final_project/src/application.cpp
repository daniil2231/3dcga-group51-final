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

        m_meshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/model.obj");
        m_wheelMeshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/wheel.obj");

        Mesh ground;

        ground.vertices = {
            // Position             Normal             Texture coordinates
            { {-5.0f, 0.0f, -5.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },
            { {-5.0f, 0.0f,  5.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },
            { { 5.0f, 0.0f,  5.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },
            { { 5.0f, 0.0f, -5.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} }
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
            shadowBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "Shaders/shadow_frag.glsl");
            m_shadowShader = shadowBuilder.build();

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

            ImGui::End();

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

            // wheel transformations

            const glm::vec3 wheelCenter{ -0.00035345f, 0.00242475f, 0.00189170f };

            const glm::vec3 wheelPosition = { 1.2f, -0.42f, 0.0f };
            
            const glm::mat4 wheelCentering = glm::translate(glm::mat4(1.0f), -wheelCenter);

            const glm::mat4 wheelScale = glm::scale(glm::mat4(1.0f), glm::vec3(0.3f));

            const glm::mat4 wheelRotation = glm::rotate(glm::mat4(1.0f), glm::radians(m_wheelAngleDegrees), glm::vec3(1.0f, 0.0f, 0.0f));

            const glm::mat4 wheelPlacement = glm::translate(glm::mat4(1.0f), wheelPosition);

            const glm::mat4 wheelModelMatrix = wheelPlacement * wheelRotation * wheelScale * wheelCentering;

            //wheel uniforms

            const glm::mat4 wheelMVP = m_projectionMatrix * camera.viewMatrix() * wheelModelMatrix;
            const glm::mat3 wheelNormalMatrix = glm::inverseTranspose(glm::mat3(wheelModelMatrix));

            m_defaultShader.bind();

            glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(wheelMVP));
            glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"), 1, GL_FALSE, glm::value_ptr(wheelModelMatrix));
            glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(wheelNormalMatrix));
            glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_FALSE);
            glUniform1i(m_defaultShader.getUniformLocation("useMaterial"),GL_TRUE);

            for (GPUMesh& wheelMesh : m_wheelMeshes){
                wheelMesh.draw(m_defaultShader);
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
    Camera oldCam2{ &m_window, glm::vec3(0.0f, 4.0f, 0.5f), -glm::vec3(0.0f, 4.0f, 0.5f) };
    bool cam1Selected = true;

    //sun's direction from the surface to its position
    glm::vec3 m_sunDirection { 1.0f, 1.0f, 0.5f};
    glm::vec3 m_sunColor { 1.0f, 0.95f, 0.85f };
    float m_sunIntensity {1.0f};
    float m_ambientStreangth {0.15f};
    
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
