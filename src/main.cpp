#include <cstdio>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "AppMainWindow.h"

#ifdef _WIN32
#include <windows.h>
extern int main(int argc, char** argv);
int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return main(__argc, __argv);
}
#endif

namespace
{
    packpdf::AppMainWindow* g_MainWindow = nullptr;

    void OnDrop(GLFWwindow*, int count, const char** paths)
    {
        if (g_MainWindow)
        {
            g_MainWindow->OnFilesDropped(paths, count);
        }
    }
}

int main(int /*argc*/, char** /*argv*/)
{
    if (!glfwInit())
    {
        std::fprintf(stderr, "glfwInit failed\n");
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(720, 500, "PackPDF", nullptr, nullptr);
    if (!window)
    {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return -1;
    }

    // Floor at the size where the timeline row (badge + path + page-range
    // selector + arrow buttons + remove) and the Output panel still fit.
    glfwSetWindowSizeLimits(window, 640, 500, GLFW_DONT_CARE, GLFW_DONT_CARE);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    packpdf::AppMainWindow::LoadFonts();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    packpdf::AppMainWindow mainWindow;
    mainWindow.ApplyImGuiStyle();
    g_MainWindow = &mainWindow;
    glfwSetDropCallback(window, OnDrop);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        mainWindow.Render();

        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    g_MainWindow = nullptr;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
