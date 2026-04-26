#include <cstdio>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "App/AppMainWindow.h"
#include "App/Cli.h"

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

#ifdef _WIN32
    // WIN32_EXECUTABLE has no console; attach to parent shell so STD_*_HANDLE
    // writes land in the user's terminal. No-op when handles are already piped.
    void EnsureCliConsole()
    {
        HANDLE h = ::GetStdHandle(STD_ERROR_HANDLE);
        if (h == nullptr || h == INVALID_HANDLE_VALUE)
        {
            ::AttachConsole(ATTACH_PARENT_PROCESS);
        }
    }

    // argv from the CRT is encoded in the active ANSI code page; CJK paths
    // arrive as mojibake. Re-fetch via CommandLineToArgvW + UTF-8 conversion
    // so segment paths survive verbatim.
    std::vector<std::string> CollectArgsUtf8()
    {
        std::vector<std::string> out;
        int n = 0;
        LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &n);
        if (!wargv) return out;
        out.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            int len = ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
            if (len > 1)
            {
                ::WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len - 1, nullptr, nullptr);
            }
            out.push_back(std::move(s));
        }
        ::LocalFree(wargv);
        return out;
    }
#endif
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    std::vector<std::string> wideArgs = CollectArgsUtf8();
#else
    std::vector<std::string> wideArgs;
    for (int i = 0; i < argc; ++i) wideArgs.emplace_back(argv[i]);
#endif

    if (wideArgs.size() >= 2 && wideArgs[1] == "compose")
    {
#ifdef _WIN32
        EnsureCliConsole();
#endif
        std::vector<std::string> rest(wideArgs.begin() + 2, wideArgs.end());
        return packpdf::RunComposeCli(rest);
    }

    (void)argc; (void)argv;

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
