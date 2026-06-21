#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <gdiplus.h>

#include <Anim8orX/Import/An8Parser.hpp>
#include <Anim8orX/Viewport/Camera.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

using anim8orx::An8Document;
using anim8orx::An8Face;
using anim8orx::An8LoadResult;
using anim8orx::An8Mesh;
using anim8orx::An8Object;
using anim8orx::An8Vector3;
using anim8orx::Vec3;
using anim8orx::ViewportCamera;
using anim8orx::ViewportCameraInput;

constexpr wchar_t kWindowClassName[] = L"Anim8orXEditorWindow";
constexpr UINT_PTR kFrameTimerId = 1;
constexpr int kFrameMillis = 16;
constexpr int kMenuModeObject = 1001;
constexpr int kMenuModeFigure = 1002;
constexpr int kMenuModeSequence = 1003;
constexpr int kMenuModeScene = 1004;
constexpr int kMenuFileNew = 1200;
constexpr int kMenuFileOpen = 1201;
constexpr int kMenuFileExit = 1202;
constexpr int kMenuViewAll = 1100;
constexpr int kMenuViewFront = 1101;
constexpr int kMenuViewBack = 1102;
constexpr int kMenuViewLeft = 1103;
constexpr int kMenuViewRight = 1104;
constexpr int kMenuViewTop = 1105;
constexpr int kMenuViewBottom = 1106;
constexpr int kMenuViewOrtho = 1107;
constexpr int kMenuViewPerspective = 1108;

enum class ViewMode {
    All,
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
    Ortho,
    Perspective
};

struct RectI {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool Contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }

    RECT ToRECT() const {
        return RECT{x, y, x + w, y + h};
    }
};

struct UiLayout {
    RectI topBar;
    RectI leftPanel;
    RectI toolBar;
    RectI viewport;
    RectI rightPanel;
    RectI console;
    RectI status;
};

struct MeshView {
    std::string objectName;
    std::string meshName;
    std::vector<An8Vector3> points;
    std::vector<An8Face> faces;
};

struct ProjectionPoint {
    POINT p{};
    bool visible = false;
};

struct EditorState {
    HWND hwnd = nullptr;
    int width = 1280;
    int height = 820;
    UiLayout layout;

    HFONT font = nullptr;
    HFONT boldFont = nullptr;
    HFONT smallFont = nullptr;
    std::unique_ptr<Gdiplus::Image> logoImage;
    HICON logoIcon = nullptr;

    An8Document document;
    std::vector<MeshView> meshes;
    std::vector<std::wstring> consoleLines;
    std::filesystem::path loadedPath;

    ViewportCamera camera;
    bool viewportActive = false;
    bool rightMouseDown = false;
    bool leftMouseDown = false;
    bool middleMouseDown = false;
    bool focusPressed = false;
    bool viewMenuOpen = false;
    POINT lastMouse = {};
    float accumulatedMouseDx = 0.0f;
    float accumulatedMouseDy = 0.0f;
    float accumulatedWheel = 0.0f;

    Vec3 selectionCenter = {0.0f, 0.0f, 0.0f};
    float selectionRadius = 1.5f;

    int activeMode = 0;
    int activeTool = 0;
    int propertyPage = 3;
    int selectedMesh = 0;
    ViewMode activeView = ViewMode::Perspective;
};

EditorState g_editor;
ULONG_PTR g_gdiplusToken = 0;

std::wstring ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (count <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring wide(static_cast<size_t>(count - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), count);
    return wide;
}

std::filesystem::path ExecutableDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0) {
        return std::filesystem::current_path();
    }

    return std::filesystem::path(std::wstring(buffer, buffer + length)).parent_path();
}

std::filesystem::path FindBundledSample() {
    const std::filesystem::path exeDir = ExecutableDirectory();
    const std::vector<std::filesystem::path> candidates = {
        exeDir / "examples" / "cube.an8",
        exeDir.parent_path() / "examples" / "cube.an8",
        std::filesystem::current_path() / "examples" / "cube.an8"
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

std::filesystem::path FindBundledLogo() {
    const std::filesystem::path exeDir = ExecutableDirectory();
    const std::vector<std::filesystem::path> candidates = {
        exeDir / "assets" / "wlogo.png",
        exeDir.parent_path() / "assets" / "wlogo.png",
        std::filesystem::current_path() / "assets" / "wlogo.png"
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

void AddConsoleLine(EditorState& editor, const std::wstring& line);

void LoadLogo(EditorState& editor) {
    const std::filesystem::path logoPath = FindBundledLogo();
    if (logoPath.empty()) {
        AddConsoleLine(editor, L"Logo asset not found.");
        return;
    }

    const std::wstring widePath = logoPath.wstring();
    auto image = std::make_unique<Gdiplus::Image>(widePath.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok) {
        AddConsoleLine(editor, L"Failed to load logo asset.");
        return;
    }

    editor.logoImage = std::move(image);

    Gdiplus::Bitmap iconBitmap(widePath.c_str());
    if (iconBitmap.GetLastStatus() == Gdiplus::Ok) {
        HICON icon = nullptr;
        if (iconBitmap.GetHICON(&icon) == Gdiplus::Ok && icon != nullptr) {
            editor.logoIcon = icon;
        }
    }

    AddConsoleLine(editor, L"Loaded logo: " + logoPath.wstring());
}

An8Document BuildFallbackCubeDocument() {
    An8Document document;
    An8Object object;
    object.name = "Default Cube";

    An8Mesh mesh;
    mesh.name = "Cube Mesh";
    mesh.points = {
        {-1.0f, -1.0f, -1.0f},
        { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f},
        {-1.0f,  1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f},
        { 1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f},
        {-1.0f,  1.0f,  1.0f}
    };
    mesh.faces = {
        {{0, 1, 2, 3}},
        {{4, 7, 6, 5}},
        {{0, 4, 5, 1}},
        {{1, 5, 6, 2}},
        {{2, 6, 7, 3}},
        {{3, 7, 4, 0}}
    };

    object.meshes.push_back(std::move(mesh));
    document.objects.push_back(std::move(object));
    return document;
}

void AddConsoleLine(EditorState& editor, const std::wstring& line) {
    editor.consoleLines.push_back(line);
    if (editor.consoleLines.size() > 128) {
        editor.consoleLines.erase(editor.consoleLines.begin());
    }
}

void CollectMeshesFromObject(const An8Object& object, std::vector<MeshView>& meshes) {
    for (const An8Mesh& mesh : object.meshes) {
        MeshView view;
        view.objectName = object.name;
        view.meshName = mesh.name;
        view.points = mesh.points;
        view.faces = mesh.faces;
        meshes.push_back(std::move(view));
    }

    for (const An8Object& child : object.children) {
        CollectMeshesFromObject(child, meshes);
    }
}

void RebuildMeshViews(EditorState& editor) {
    editor.meshes.clear();
    for (const An8Object& object : editor.document.objects) {
        CollectMeshesFromObject(object, editor.meshes);
    }

    editor.selectedMesh = editor.meshes.empty()
        ? -1
        : std::clamp(editor.selectedMesh, 0, static_cast<int>(editor.meshes.size()) - 1);
}

void RecalculateSelectionBounds(EditorState& editor) {
    if (editor.meshes.empty()) {
        editor.selectionCenter = {0.0f, 0.0f, 0.0f};
        editor.selectionRadius = 1.5f;
        return;
    }

    Vec3 minPoint = {999999.0f, 999999.0f, 999999.0f};
    Vec3 maxPoint = {-999999.0f, -999999.0f, -999999.0f};
    size_t pointCount = 0;

    for (const MeshView& mesh : editor.meshes) {
        for (const An8Vector3& point : mesh.points) {
            minPoint.x = std::min(minPoint.x, point.x);
            minPoint.y = std::min(minPoint.y, point.y);
            minPoint.z = std::min(minPoint.z, point.z);
            maxPoint.x = std::max(maxPoint.x, point.x);
            maxPoint.y = std::max(maxPoint.y, point.y);
            maxPoint.z = std::max(maxPoint.z, point.z);
            ++pointCount;
        }
    }

    if (pointCount == 0) {
        editor.selectionCenter = {0.0f, 0.0f, 0.0f};
        editor.selectionRadius = 1.5f;
        return;
    }

    editor.selectionCenter = (minPoint + maxPoint) * 0.5f;
    editor.selectionRadius = std::max(anim8orx::Length(maxPoint - editor.selectionCenter), 0.75f);
}

std::wstring ViewModeName(ViewMode mode) {
    switch (mode) {
        case ViewMode::All: return L"All";
        case ViewMode::Front: return L"Front";
        case ViewMode::Back: return L"Back";
        case ViewMode::Left: return L"Left";
        case ViewMode::Right: return L"Right";
        case ViewMode::Top: return L"Top";
        case ViewMode::Bottom: return L"Bottom";
        case ViewMode::Ortho: return L"Ortho";
        case ViewMode::Perspective: return L"Perspective";
        default: return L"Perspective";
    }
}

void SetViewportView(EditorState& editor, ViewMode mode) {
    editor.activeView = mode;
    editor.viewMenuOpen = false;

    const Vec3 c = editor.selectionCenter;
    const float d = std::max(editor.selectionRadius * 3.2f, 4.0f);

    switch (mode) {
        case ViewMode::Front:
        case ViewMode::Ortho:
            editor.camera.SetLookAt({c.x, c.y, c.z + d}, c);
            break;
        case ViewMode::Back:
            editor.camera.SetLookAt({c.x, c.y, c.z - d}, c);
            break;
        case ViewMode::Left:
            editor.camera.SetLookAt({c.x - d, c.y, c.z}, c);
            break;
        case ViewMode::Right:
            editor.camera.SetLookAt({c.x + d, c.y, c.z}, c);
            break;
        case ViewMode::Top:
            editor.camera.SetLookAt({c.x, c.y + d, c.z}, c);
            break;
        case ViewMode::Bottom:
            editor.camera.SetLookAt({c.x, c.y - d, c.z}, c);
            break;
        case ViewMode::All:
        case ViewMode::Perspective:
        default:
            editor.camera.SetLookAt({c.x + d * 0.85f, c.y + d * 0.65f, c.z + d}, c);
            editor.activeView = mode == ViewMode::All ? ViewMode::All : ViewMode::Perspective;
            break;
    }

    editor.camera.FocusOn(c, editor.selectionRadius);
    AddConsoleLine(editor, L"Viewport switched to " + ViewModeName(editor.activeView) + L".");
}

bool LoadDocument(EditorState& editor, const std::filesystem::path& requestedPath) {
    std::filesystem::path path = requestedPath;
    if (path.empty()) {
        path = FindBundledSample();
    }

    if (!path.empty()) {
        const An8LoadResult result = anim8orx::LoadAn8File(path.string());
        if (result.ok && !result.document.objects.empty()) {
            editor.document = result.document;
            editor.loadedPath = path;
            AddConsoleLine(editor, L"Loaded .an8 file: " + path.wstring());

            for (const std::string& warning : result.warnings) {
                AddConsoleLine(editor, L"Warning: " + ToWide(warning));
            }
        } else {
            editor.document = BuildFallbackCubeDocument();
            AddConsoleLine(editor, L"Could not load requested .an8 file. Using built-in cube.");
            for (const std::string& error : result.errors) {
                AddConsoleLine(editor, L"Error: " + ToWide(error));
            }
        }
    } else {
        editor.document = BuildFallbackCubeDocument();
        AddConsoleLine(editor, L"No bundled .an8 sample found. Using built-in cube.");
    }

    RebuildMeshViews(editor);
    RecalculateSelectionBounds(editor);
    SetViewportView(editor, ViewMode::Perspective);
    return !editor.meshes.empty();
}

void RefreshWindowTitle(EditorState& editor) {
    if (editor.hwnd == nullptr) {
        return;
    }

    std::wstring title = L"Anim8orX";
    if (!editor.loadedPath.empty()) {
        title += L" - ";
        title += editor.loadedPath.filename().wstring();
    }
    SetWindowTextW(editor.hwnd, title.c_str());
}

void LoadDocumentAndRefresh(EditorState& editor, const std::filesystem::path& path) {
    if (LoadDocument(editor, path)) {
        RefreshWindowTitle(editor);
    }
    InvalidateRect(editor.hwnd, nullptr, FALSE);
}

std::filesystem::path ShowOpenAn8Dialog(HWND owner) {
    wchar_t fileName[MAX_PATH] = {};

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Anim8or Files (*.an8)\0*.an8\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"an8";
    ofn.lpstrTitle = L"Open Anim8or .an8 File";

    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }

    return std::filesystem::path(fileName);
}

void OpenAn8FromDialog(EditorState& editor) {
    const std::filesystem::path path = ShowOpenAn8Dialog(editor.hwnd);
    if (path.empty()) {
        AddConsoleLine(editor, L"Open .an8 cancelled.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    LoadDocumentAndRefresh(editor, path);
}

void NewDefaultDocument(EditorState& editor) {
    editor.document = BuildFallbackCubeDocument();
    editor.loadedPath.clear();
    RebuildMeshViews(editor);
    RecalculateSelectionBounds(editor);
    SetViewportView(editor, ViewMode::Perspective);
    AddConsoleLine(editor, L"Created new default Anim8orX scene.");
    RefreshWindowTitle(editor);
    InvalidateRect(editor.hwnd, nullptr, FALSE);
}

void HandleDroppedFiles(EditorState& editor, HDROP drop) {
    wchar_t pathBuffer[MAX_PATH] = {};
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

    if (count == 0 || DragQueryFileW(drop, 0, pathBuffer, MAX_PATH) == 0) {
        DragFinish(drop);
        AddConsoleLine(editor, L"Drop did not contain a readable file.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    DragFinish(drop);

    const std::filesystem::path path(pathBuffer);
    if (path.extension() != L".an8" && path.extension() != L".AN8") {
        AddConsoleLine(editor, L"Dropped file is not an .an8 file: " + path.filename().wstring());
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    LoadDocumentAndRefresh(editor, path);
}

std::vector<std::wstring> CommandLineArguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;

    if (argv != nullptr) {
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        LocalFree(argv);
    }

    return args;
}

COLORREF Rgb(int r, int g, int b) {
    return RGB(r, g, b);
}

HBRUSH CreateBrush(COLORREF color) {
    return CreateSolidBrush(color);
}

HPEN CreatePenSolid(COLORREF color, int width = 1) {
    return CreatePen(PS_SOLID, width, color);
}

void Fill(HDC dc, const RectI& rect, COLORREF color) {
    HBRUSH brush = CreateBrush(color);
    RECT nativeRect = rect.ToRECT();
    FillRect(dc, &nativeRect, brush);
    DeleteObject(brush);
}

void Stroke(HDC dc, const RectI& rect, COLORREF color) {
    HPEN pen = CreatePenSolid(color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CreatePenSolid(color, width);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void Text(HDC dc, const std::wstring& text, int x, int y, int w, int h, COLORREF color, HFONT font, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    RECT rect{x, y, x + w, y + h};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HGDIOBJ oldFont = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), -1, &rect, format);
    SelectObject(dc, oldFont);
}

void CalculateLayout(EditorState& editor) {
    const int commandStrip = 32;
    const int status = 20;
    const int rail = 62;
    const int right = editor.width >= 980 ? 344 : 0;
    const int bottom = editor.height >= 680 ? 128 : 92;

    editor.layout.topBar = {0, 0, editor.width, commandStrip};
    editor.layout.leftPanel = {0, commandStrip, rail, editor.height - commandStrip - status};
    editor.layout.toolBar = {0, 0, 0, 0};
    editor.layout.rightPanel = {editor.width - right, commandStrip, right, editor.height - commandStrip - bottom - status};
    editor.layout.console = {rail, editor.height - bottom - status, editor.width - rail, bottom};
    editor.layout.viewport = {rail, commandStrip, editor.width - rail - right, editor.height - commandStrip - bottom - status};
    editor.layout.status = {0, editor.height - status, editor.width, status};
}

ProjectionPoint ProjectPoint(const EditorState& editor, const An8Vector3& point) {
    ProjectionPoint projected;
    const RectI& viewport = editor.layout.viewport;

    const Vec3 world = {point.x, point.y, point.z};
    const Vec3 rel = world - editor.camera.position;
    const float z = anim8orx::Dot(rel, editor.camera.Forward());
    if (z <= 0.02f) {
        return projected;
    }

    const float x = anim8orx::Dot(rel, editor.camera.Right());
    const float y = anim8orx::Dot(rel, editor.camera.Up());
    const float aspect = viewport.h > 0
        ? static_cast<float>(viewport.w) / static_cast<float>(viewport.h)
        : 1.0f;
    const float tanHalfFov = std::tan(editor.camera.verticalFovRadians * 0.5f);

    const float ndcX = x / (z * tanHalfFov * aspect);
    const float ndcY = y / (z * tanHalfFov);

    projected.p.x = viewport.x + static_cast<LONG>((ndcX * 0.5f + 0.5f) * static_cast<float>(viewport.w));
    projected.p.y = viewport.y + static_cast<LONG>((0.5f - ndcY * 0.5f) * static_cast<float>(viewport.h));
    projected.visible = projected.p.x > viewport.x - 1000 &&
                        projected.p.x < viewport.x + viewport.w + 1000 &&
                        projected.p.y > viewport.y - 1000 &&
                        projected.p.y < viewport.y + viewport.h + 1000;
    return projected;
}

void DrawWorldLine(HDC dc, const EditorState& editor, const An8Vector3& a, const An8Vector3& b, COLORREF color, int width = 1) {
    const ProjectionPoint pa = ProjectPoint(editor, a);
    const ProjectionPoint pb = ProjectPoint(editor, b);
    if (!pa.visible || !pb.visible) {
        return;
    }

    DrawLine(dc, pa.p.x, pa.p.y, pb.p.x, pb.p.y, color, width);
}

void DrawPanelHeader(HDC dc, const EditorState& editor, const RectI& rect, const std::wstring& title) {
    Fill(dc, {rect.x, rect.y, rect.w, 30}, Rgb(37, 40, 47));
    Text(dc, title, rect.x + 12, rect.y, rect.w - 24, 30, Rgb(231, 236, 241), editor.boldFont);
    DrawLine(dc, rect.x, rect.y + 30, rect.x + rect.w, rect.y + 30, Rgb(74, 80, 91));
}

void DrawSmallButton(HDC dc, const EditorState& editor, const RectI& rect, const std::wstring& label, bool active = false) {
    Fill(dc, rect, active ? Rgb(50, 50, 45) : Rgb(43, 44, 45));
    Stroke(dc, rect, active ? Rgb(255, 140, 0) : Rgb(88, 88, 88));
    Text(dc, label, rect.x, rect.y, rect.w, rect.h, active ? Rgb(255, 148, 0) : Rgb(184, 184, 184), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawIconGlyph(HDC dc, const RectI& rect, int glyph, COLORREF color) {
    const int cx = rect.x + rect.w / 2;
    const int cy = rect.y + rect.h / 2;
    HPEN pen = CreatePenSolid(color, 1);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));

    switch (glyph % 12) {
        case 0:
            MoveToEx(dc, rect.x + 6, rect.y + 5, nullptr);
            LineTo(dc, rect.x + 7, rect.y + rect.h - 6);
            LineTo(dc, rect.x + 14, rect.y + rect.h - 11);
            LineTo(dc, rect.x + 10, rect.y + rect.h - 12);
            LineTo(dc, rect.x + 16, rect.y + rect.h - 5);
            break;
        case 1:
            Rectangle(dc, rect.x + 6, rect.y + 6, rect.x + rect.w - 6, rect.y + rect.h - 6);
            break;
        case 2:
            Ellipse(dc, rect.x + 5, rect.y + 5, rect.x + rect.w - 5, rect.y + rect.h - 5);
            break;
        case 3:
            MoveToEx(dc, rect.x + 5, cy, nullptr);
            LineTo(dc, rect.x + rect.w - 5, cy);
            MoveToEx(dc, cx, rect.y + 5, nullptr);
            LineTo(dc, cx, rect.y + rect.h - 5);
            break;
        case 4:
            MoveToEx(dc, rect.x + 6, rect.y + rect.h - 7, nullptr);
            LineTo(dc, rect.x + rect.w - 7, rect.y + 6);
            LineTo(dc, rect.x + rect.w - 8, rect.y + 14);
            MoveToEx(dc, rect.x + rect.w - 7, rect.y + 6, nullptr);
            LineTo(dc, rect.x + rect.w - 15, rect.y + 6);
            break;
        case 5:
            Arc(dc, rect.x + 5, rect.y + 5, rect.x + rect.w - 5, rect.y + rect.h - 5, rect.x + rect.w - 6, cy, cx, rect.y + 5);
            MoveToEx(dc, cx, rect.y + 5, nullptr);
            LineTo(dc, cx + 4, rect.y + 5);
            break;
        case 6:
            MoveToEx(dc, rect.x + 5, rect.y + rect.h - 6, nullptr);
            LineTo(dc, rect.x + rect.w - 6, rect.y + 6);
            Rectangle(dc, rect.x + 5, rect.y + 5, rect.x + rect.w - 8, rect.y + rect.h - 8);
            break;
        case 7:
            Rectangle(dc, rect.x + 7, rect.y + 8, rect.x + rect.w - 7, rect.y + rect.h - 6);
            MoveToEx(dc, rect.x + 7, rect.y + 8, nullptr);
            LineTo(dc, rect.x + 13, rect.y + 3);
            LineTo(dc, rect.x + rect.w - 4, rect.y + 3);
            LineTo(dc, rect.x + rect.w - 7, rect.y + 8);
            break;
        case 8:
            Ellipse(dc, rect.x + 4, rect.y + 5, rect.x + rect.w - 4, rect.y + rect.h - 5);
            MoveToEx(dc, cx, rect.y + 5, nullptr);
            LineTo(dc, cx, rect.y + rect.h - 5);
            MoveToEx(dc, rect.x + 5, cy, nullptr);
            LineTo(dc, rect.x + rect.w - 5, cy);
            break;
        case 9:
            MoveToEx(dc, rect.x + 7, rect.y + rect.h - 7, nullptr);
            LineTo(dc, cx, rect.y + 5);
            LineTo(dc, rect.x + rect.w - 7, rect.y + rect.h - 7);
            LineTo(dc, rect.x + 7, rect.y + rect.h - 7);
            break;
        case 10:
            MoveToEx(dc, rect.x + 5, rect.y + rect.h - 6, nullptr);
            LineTo(dc, rect.x + 9, rect.y + 6);
            LineTo(dc, rect.x + rect.w - 8, rect.y + 7);
            LineTo(dc, rect.x + rect.w - 5, rect.y + rect.h - 6);
            LineTo(dc, rect.x + 5, rect.y + rect.h - 6);
            break;
        default:
            MoveToEx(dc, rect.x + 5, rect.y + rect.h - 5, nullptr);
            LineTo(dc, cx, rect.y + 5);
            LineTo(dc, rect.x + rect.w - 5, rect.y + rect.h - 5);
            MoveToEx(dc, rect.x + 8, cy, nullptr);
            LineTo(dc, rect.x + rect.w - 8, cy);
            break;
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawIconButton(HDC dc, const EditorState& editor, const RectI& rect, int glyph, bool active = false) {
    (void)editor;
    Fill(dc, rect, active ? Rgb(50, 43, 33) : Rgb(38, 39, 40));
    Stroke(dc, rect, active ? Rgb(255, 140, 0) : Rgb(76, 76, 76));
    DrawIconGlyph(dc, rect, glyph, active ? Rgb(255, 148, 0) : Rgb(168, 168, 168));
}

void DrawTopBar(HDC dc, const EditorState& editor) {
    const RectI& bar = editor.layout.topBar;
    Fill(dc, bar, Rgb(48, 48, 48));
    DrawLine(dc, bar.x, bar.y + bar.h - 1, bar.x + bar.w, bar.y + bar.h - 1, Rgb(10, 10, 10));

    if (editor.logoImage != nullptr) {
        Gdiplus::Graphics graphics(dc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(editor.logoImage.get(), Gdiplus::Rect(bar.x + 4, bar.y + 3, 26, 26));
    } else {
        Fill(dc, {bar.x + 5, bar.y + 5, 22, 22}, Rgb(255, 148, 0));
        Text(dc, L"X", bar.x + 5, bar.y + 5, 22, 22, Rgb(20, 20, 20), editor.boldFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    Text(dc, L"Anim8orX", bar.x + 36, bar.y, 86, bar.h, Rgb(255, 148, 0), editor.boldFont);

    int x = 128;
    for (int i = 0; i < 15; ++i) {
        DrawIconButton(dc, editor, {x, bar.y + 4, 20, 20}, i, i == editor.activeTool);
        x += 23;
        if (i == 5 || i == 9) {
            x += 8;
        }
    }

    x += 18;
    for (int i = 0; i <= 7; ++i) {
        Text(dc, std::to_wstring(i), x, bar.y + 1, 18, bar.h - 2, i == 0 ? Rgb(255, 148, 0) : Rgb(115, 115, 115), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += 19;
    }

    const wchar_t* modes[] = {L"Object", L"Figure", L"Sequence", L"Scene"};
    int modeX = std::max(x + 24, bar.w - 238);
    for (int i = 0; i < 4; ++i) {
        const int tabW = i == 0 ? 50 : 62;
        RectI tab{modeX, bar.y + 3, tabW, 22};
        Fill(dc, tab, i == editor.activeMode ? Rgb(33, 33, 33) : Rgb(43, 43, 43));
        Stroke(dc, tab, i == editor.activeMode ? Rgb(255, 140, 0) : Rgb(58, 58, 58));
        Text(dc, modes[i], tab.x, tab.y, tab.w, tab.h, i == editor.activeMode ? Rgb(255, 148, 0) : Rgb(125, 125, 125), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        modeX += tabW + 4;
    }
}

void DrawToolBar(HDC dc, const EditorState& editor) {
    const RectI& rail = editor.layout.leftPanel;
    Fill(dc, rail, Rgb(43, 43, 43));
    DrawLine(dc, rail.x + rail.w - 1, rail.y, rail.x + rail.w - 1, rail.y + rail.h, Rgb(5, 5, 5));

    struct ToolGroup {
        const wchar_t* title;
        int count;
        int firstGlyph;
    };

    const ToolGroup groups[] = {
        {L"Mode", 4, 0},
        {L"Coord", 4, 3},
        {L"Axis", 3, 8},
        {L"Tools", 10, 0},
        {L"UV", 2, 6},
        {L"Shapes", 9, 7},
        {L"Rig", 4, 10},
        {L"Keys", 4, 4}
    };

    int y = rail.y + 2;
    int toolIndex = 0;
    for (const ToolGroup& group : groups) {
        if (y + 18 >= rail.y + rail.h) {
            break;
        }

        Fill(dc, {rail.x, y, rail.w - 1, 16}, Rgb(34, 34, 34));
        Text(dc, group.title, rail.x + 2, y, rail.w - 4, 16, Rgb(164, 164, 164), editor.smallFont);
        y += 18;

        if (wcscmp(group.title, L"Axis") == 0) {
            const wchar_t* axes[] = {L"X", L"Y", L"Z"};
            for (int i = 0; i < 3; ++i) {
                DrawSmallButton(dc, editor, {rail.x + 2 + i * 18, y, 17, 18}, axes[i], i == 0);
            }
            y += 23;
            continue;
        }

        for (int i = 0; i < group.count; ++i) {
            if (y + 22 >= rail.y + rail.h) {
                break;
            }

            const int col = i % 2;
            const int row = i / 2;
            RectI button{rail.x + 4 + col * 27, y + row * 27, 22, 22};
            DrawIconButton(dc, editor, button, group.firstGlyph + i, toolIndex == editor.activeTool);
            ++toolIndex;
        }

        y += ((group.count + 1) / 2) * 27 + 6;
    }
}

void DrawHierarchy(HDC dc, const EditorState& editor) {
    const RectI& panel = editor.layout.leftPanel;
    Fill(dc, panel, Rgb(26, 28, 34));
    Stroke(dc, panel, Rgb(68, 74, 84));
    DrawPanelHeader(dc, editor, panel, L"Hierarchy");

    int y = panel.y + 40;
    Text(dc, L"Scene", panel.x + 12, y, panel.w - 24, 22, Rgb(197, 206, 216), editor.boldFont);
    y += 26;

    for (size_t i = 0; i < editor.meshes.size(); ++i) {
        const MeshView& mesh = editor.meshes[i];
        const bool selected = static_cast<int>(i) == editor.selectedMesh;
        RectI row{panel.x + 8, y, panel.w - 16, 48};
        Fill(dc, row, selected ? Rgb(48, 78, 88) : Rgb(30, 33, 39));
        Stroke(dc, row, selected ? Rgb(92, 174, 188) : Rgb(50, 55, 65));
        Text(dc, L"> " + ToWide(mesh.objectName), row.x + 10, row.y + 4, row.w - 20, 20, Rgb(235, 239, 243), editor.font);
        Text(dc, L"  Mesh: " + ToWide(mesh.meshName), row.x + 10, row.y + 24, row.w - 20, 18, Rgb(166, 175, 187), editor.smallFont);
        y += 54;
    }

    if (editor.meshes.empty()) {
        Text(dc, L"No mesh nodes loaded", panel.x + 12, y, panel.w - 24, 24, Rgb(166, 175, 187), editor.font);
    }
}

void DrawInspectorRow(HDC dc, const EditorState& editor, int x, int y, const std::wstring& label, const std::wstring& value) {
    Text(dc, label, x, y, 126, 22, Rgb(171, 181, 192), editor.smallFont);
    RectI field{x + 130, y + 2, 178, 20};
    Fill(dc, field, Rgb(20, 22, 25));
    Stroke(dc, field, Rgb(64, 70, 80));
    Text(dc, value, field.x + 7, field.y, field.w - 14, field.h, Rgb(230, 235, 240), editor.smallFont);
}

void DrawCheckboxRow(HDC dc, const EditorState& editor, int x, int y, const std::wstring& label, bool checked) {
    RectI box{x, y + 4, 13, 13};
    Fill(dc, box, Rgb(18, 20, 23));
    Stroke(dc, box, checked ? Rgb(255, 148, 0) : Rgb(88, 88, 88));
    if (checked) {
        DrawLine(dc, box.x + 3, box.y + 7, box.x + 6, box.y + 10, Rgb(255, 148, 0), 2);
        DrawLine(dc, box.x + 6, box.y + 10, box.x + 11, box.y + 3, Rgb(255, 148, 0), 2);
    }
    Text(dc, label, x + 20, y, 288, 22, Rgb(205, 210, 216), editor.smallFont);
}

void DrawSliderRow(HDC dc, const EditorState& editor, int x, int y, const std::wstring& label, float normalized, const std::wstring& value) {
    Text(dc, label, x, y, 126, 22, Rgb(171, 181, 192), editor.smallFont);
    RectI track{x + 130, y + 8, 116, 6};
    Fill(dc, track, Rgb(22, 24, 28));
    Stroke(dc, track, Rgb(64, 70, 80));
    RectI fill{track.x + 1, track.y + 1, static_cast<int>((track.w - 2) * std::clamp(normalized, 0.0f, 1.0f)), track.h - 2};
    Fill(dc, fill, Rgb(255, 148, 0));
    Text(dc, value, x + 252, y, 56, 22, Rgb(230, 235, 240), editor.smallFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void DrawSectionTitle(HDC dc, const EditorState& editor, int x, int& y, const std::wstring& title) {
    Fill(dc, {x, y, 310, 22}, Rgb(39, 43, 49));
    Stroke(dc, {x, y, 310, 22}, Rgb(71, 78, 88));
    Text(dc, L"v  " + title, x + 8, y, 294, 22, Rgb(238, 241, 245), editor.boldFont);
    y += 28;
}

void DrawComboRow(HDC dc, const EditorState& editor, int x, int y, const std::wstring& label, const std::wstring& value) {
    DrawInspectorRow(dc, editor, x, y, label, value + L"  v");
}

std::wstring FormatFloat(float value) {
    std::wostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << value;
    return out.str();
}

void DrawInspector(HDC dc, const EditorState& editor) {
    const RectI& panel = editor.layout.rightPanel;
    if (panel.w <= 0 || panel.h <= 0) {
        return;
    }

    Fill(dc, panel, Rgb(24, 27, 32));
    Stroke(dc, panel, Rgb(75, 82, 92));
    DrawPanelHeader(dc, editor, panel, L"Anim8orX Properties");

    HRGN clip = CreateRectRgn(panel.x + 1, panel.y + 1, panel.x + panel.w - 1, panel.y + panel.h - 1);
    SelectClipRgn(dc, clip);

    const wchar_t* tabs[] = {L"Setup", L"View", L"Material", L"Object", L"Figure", L"Sequence", L"Scene", L"Render"};
    int tabX = panel.x + 8;
    int tabY = panel.y + 36;
    for (int i = 0; i < 8; ++i) {
        RectI tab{tabX, tabY, i == 5 ? 72 : 58, 22};
        Fill(dc, tab, i == editor.propertyPage ? Rgb(44, 45, 39) : Rgb(33, 36, 42));
        Stroke(dc, tab, i == editor.propertyPage ? Rgb(255, 148, 0) : Rgb(61, 68, 78));
        Text(dc, tabs[i], tab.x, tab.y, tab.w, tab.h, i == editor.propertyPage ? Rgb(255, 148, 0) : Rgb(168, 176, 188), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        tabX += tab.w + 4;
        if (tabX + 64 > panel.x + panel.w - 8) {
            tabX = panel.x + 8;
            tabY += 26;
        }
    }

    int y = tabY + 34;
    const int x = panel.x + 14;
    const MeshView* mesh = editor.selectedMesh >= 0 && editor.selectedMesh < static_cast<int>(editor.meshes.size())
        ? &editor.meshes[static_cast<size_t>(editor.selectedMesh)]
        : nullptr;

    switch (editor.propertyPage) {
        case 0:
            DrawSectionTitle(dc, editor, x, y, L"Global Preferences");
            DrawInspectorRow(dc, editor, x, y, L"Grid Width", L"100.00"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Grid Snap", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Grid Snap Size", L"0.25"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Angle Snap", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Angle Snap Size", L"15.0 deg"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Backface Culling", false); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Auto-Save", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Interval", L"5 min"); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"Viewport Overlays");
            DrawCheckboxRow(dc, editor, x, y, L"Show Grid", true); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Show Axes", true); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Show Normals", false); y += 22;
            DrawSliderRow(dc, editor, x, y, L"Normal Scale", 0.35f, L"0.35"); y += 24;
            break;

        case 1:
            DrawSectionTitle(dc, editor, x, y, L"Display Mode Flags");
            DrawCheckboxRow(dc, editor, x, y, L"Wireframe", true); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Flat Shaded", false); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Smooth Shaded", false); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Textured", false); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"Camera");
            DrawInspectorRow(dc, editor, x, y, L"FOV", FormatFloat(editor.camera.verticalFovRadians / anim8orx::AX_DEG_TO_RAD)); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Near Clip", FormatFloat(editor.camera.nearPlane)); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Far Clip", FormatFloat(editor.camera.farPlane)); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Fly Speed", FormatFloat(editor.camera.flySpeed)); y += 24;
            break;

        case 2:
            DrawSectionTitle(dc, editor, x, y, L"Material Editor");
            DrawInspectorRow(dc, editor, x, y, L"Name", L"default_mat"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Ambient RGB", L"70, 70, 70"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Diffuse RGB", L"220, 180, 80"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Specular RGB", L"255, 255, 255"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Emissive RGB", L"0, 0, 0"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Brilliance", 0.45f, L"0.45"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Roughness", 0.30f, L"0.30"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Transparency", 0.00f, L"0.00"); y += 28;
            DrawSectionTitle(dc, editor, x, y, L"Texture Maps");
            DrawInspectorRow(dc, editor, x, y, L"Diffuse Map", L"<none>"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Lock Aspect Ratio", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Bump Map", L"<none>"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Bump Amplitude", L"1.00"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Specular Map", L"<none>"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Transparency Map", L"<none>"); y += 24;
            break;

        case 3:
            DrawSectionTitle(dc, editor, x, y, L"Primitive Parameters");
            DrawInspectorRow(dc, editor, x, y, L"Cube Name", mesh ? ToWide(mesh->meshName) : L"cube"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"X/Y/Z Size", L"2.0, 2.0, 2.0"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"X/Y/Z Divs", L"1, 1, 1"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Sphere Radius", L"1.0"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Lat / Long", L"12 / 24"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Top Radius", L"1.0"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Bottom Radius", L"1.0"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Height", L"2.0"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Cap Top", true); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Cap Bottom", true); y += 28;
            DrawSectionTitle(dc, editor, x, y, L"Modifiers");
            DrawInspectorRow(dc, editor, x, y, L"Lathe Degrees", L"360.0 deg"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Lathe Segments", L"24"); y += 24;
            DrawComboRow(dc, editor, x, y, L"Lathe Axis", L"Y"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Subdivision", L"1"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Smooth Angle", L"45.0 deg"); y += 24;
            break;

        case 4:
            DrawSectionTitle(dc, editor, x, y, L"Bone Segment");
            DrawInspectorRow(dc, editor, x, y, L"Name", L"bone01"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Length", L"1.00"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Inner Radius", L"0.25"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Outer Radius", L"0.75"); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"Degrees of Freedom");
            for (const wchar_t* axis : {L"X Axis", L"Y Axis", L"Z Axis"}) {
                DrawCheckboxRow(dc, editor, x, y, std::wstring(axis) + L" Rotation", true); y += 22;
                DrawInspectorRow(dc, editor, x, y, std::wstring(axis) + L" Min", L"-90.0 deg"); y += 24;
                DrawInspectorRow(dc, editor, x, y, std::wstring(axis) + L" Max", L"90.0 deg"); y += 24;
            }
            break;

        case 5:
            DrawSectionTitle(dc, editor, x, y, L"Sequence");
            DrawInspectorRow(dc, editor, x, y, L"Name", L"idle_loop"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Frames", L"48"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"FPS", L"24"); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"Keyframe Interpolation");
            DrawComboRow(dc, editor, x, y, L"Type", L"Spline"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Tension", 0.50f, L"0.50"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Continuity", 0.50f, L"0.50"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Bias", 0.50f, L"0.50"); y += 24;
            break;

        case 6:
            DrawSectionTitle(dc, editor, x, y, L"Actor Instance");
            DrawInspectorRow(dc, editor, x, y, L"Name", mesh ? ToWide(mesh->objectName) : L"object01"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Position", L"0.0, 0.0, 0.0"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Rotation", L"0.0, 0.0, 0.0"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Scale", L"1.0, 1.0, 1.0"); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"Camera Track");
            DrawInspectorRow(dc, editor, x, y, L"Focal Length", L"35.0 mm"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"FOV", L"60.0 deg"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Near / Far", L"0.01 / 10000"); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"Light Source");
            DrawComboRow(dc, editor, x, y, L"Type", L"Spotlight"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Color RGB", L"255, 244, 220"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Intensity", 0.65f, L"1.00"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Inner Cone", L"25.0 deg"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Outer Cone", L"45.0 deg"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Cast Shadows", true); y += 22;
            DrawComboRow(dc, editor, x, y, L"Shadow Map", L"1024"); y += 24;
            DrawSliderRow(dc, editor, x, y, L"Shadow Blur", 0.25f, L"0.25"); y += 24;
            break;

        default:
            DrawSectionTitle(dc, editor, x, y, L"Render Output");
            DrawComboRow(dc, editor, x, y, L"Renderer", L"ART Ray Tracer"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Width / Height", L"1280 / 720"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Lock Aspect Ratio", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Start / End", L"0 / 48"); y += 24;
            DrawInspectorRow(dc, editor, x, y, L"Frame Rate", L"24"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Anti-Aliasing", true); y += 22;
            DrawComboRow(dc, editor, x, y, L"Samples", L"3x3"); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"ART Quality");
            DrawCheckboxRow(dc, editor, x, y, L"Render Reflections", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Max Ray Depth", L"4"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Render Shadows", true); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Ambient Occlusion", true); y += 22;
            DrawSliderRow(dc, editor, x, y, L"AO Factor", 0.45f, L"0.45"); y += 24;
            break;
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

void DrawGridPlaneXZ(HDC dc, const EditorState& editor, float y, int extent, COLORREF minor, COLORREF major) {
    for (int i = -extent; i <= extent; ++i) {
        const COLORREF color = i == 0 || i % 5 == 0 ? major : minor;
        DrawWorldLine(dc, editor, {static_cast<float>(i), y, static_cast<float>(-extent)}, {static_cast<float>(i), y, static_cast<float>(extent)}, color);
        DrawWorldLine(dc, editor, {static_cast<float>(-extent), y, static_cast<float>(i)}, {static_cast<float>(extent), y, static_cast<float>(i)}, color);
    }
}

void DrawGridPlaneXY(HDC dc, const EditorState& editor, float z, int extent, COLORREF minor, COLORREF major) {
    for (int i = -extent; i <= extent; ++i) {
        const COLORREF color = i == 0 || i % 5 == 0 ? major : minor;
        DrawWorldLine(dc, editor, {static_cast<float>(i), static_cast<float>(-extent), z}, {static_cast<float>(i), static_cast<float>(extent), z}, color);
        DrawWorldLine(dc, editor, {static_cast<float>(-extent), static_cast<float>(i), z}, {static_cast<float>(extent), static_cast<float>(i), z}, color);
    }
}

void DrawGridPlaneZY(HDC dc, const EditorState& editor, float x, int extent, COLORREF minor, COLORREF major) {
    for (int i = -extent; i <= extent; ++i) {
        const COLORREF color = i == 0 || i % 5 == 0 ? major : minor;
        DrawWorldLine(dc, editor, {x, static_cast<float>(-extent), static_cast<float>(i)}, {x, static_cast<float>(extent), static_cast<float>(i)}, color);
        DrawWorldLine(dc, editor, {x, static_cast<float>(i), static_cast<float>(-extent)}, {x, static_cast<float>(i), static_cast<float>(extent)}, color);
    }
}

void DrawGridAndAxes(HDC dc, const EditorState& editor) {
    constexpr int extent = 12;
    const COLORREF minor = Rgb(63, 67, 70);
    const COLORREF major = Rgb(91, 96, 100);
    const COLORREF depth = Rgb(54, 58, 62);

    switch (editor.activeView) {
        case ViewMode::Front:
        case ViewMode::Back:
        case ViewMode::Ortho:
            DrawGridPlaneXY(dc, editor, 0.0f, extent, minor, major);
            break;
        case ViewMode::Left:
        case ViewMode::Right:
            DrawGridPlaneZY(dc, editor, 0.0f, extent, minor, major);
            break;
        case ViewMode::Top:
        case ViewMode::Bottom:
            DrawGridPlaneXZ(dc, editor, 0.0f, extent, minor, major);
            break;
        case ViewMode::All:
        case ViewMode::Perspective:
        default:
            DrawGridPlaneXZ(dc, editor, 0.0f, extent, minor, major);
            for (int i = -extent; i <= extent; i += 2) {
                DrawWorldLine(dc, editor, {static_cast<float>(i), 0.0f, static_cast<float>(-extent)}, {static_cast<float>(i), 6.0f, static_cast<float>(-extent)}, depth);
                DrawWorldLine(dc, editor, {static_cast<float>(-extent), 0.0f, static_cast<float>(i)}, {static_cast<float>(-extent), 6.0f, static_cast<float>(i)}, depth);
            }
            for (int y = 1; y <= 6; ++y) {
                const float fy = static_cast<float>(y);
                DrawWorldLine(dc, editor, {static_cast<float>(-extent), fy, static_cast<float>(-extent)}, {static_cast<float>(extent), fy, static_cast<float>(-extent)}, depth);
                DrawWorldLine(dc, editor, {static_cast<float>(-extent), fy, static_cast<float>(-extent)}, {static_cast<float>(-extent), fy, static_cast<float>(extent)}, depth);
            }
            break;
    }

    DrawWorldLine(dc, editor, {-extent, 0.0f, 0.0f}, {extent, 0.0f, 0.0f}, Rgb(190, 73, 73), 2);
    DrawWorldLine(dc, editor, {0.0f, 0.0f, -extent}, {0.0f, 0.0f, extent}, Rgb(76, 152, 88), 2);
    DrawWorldLine(dc, editor, {0.0f, -2.0f, 0.0f}, {0.0f, 8.0f, 0.0f}, Rgb(86, 134, 220), 2);
}

void DrawMeshes(HDC dc, const EditorState& editor) {
    for (size_t meshIndex = 0; meshIndex < editor.meshes.size(); ++meshIndex) {
        const MeshView& mesh = editor.meshes[meshIndex];
        const bool selected = static_cast<int>(meshIndex) == editor.selectedMesh;
        const COLORREF color = selected ? Rgb(239, 189, 87) : Rgb(192, 199, 207);
        const int width = selected ? 2 : 1;

        for (const An8Face& face : mesh.faces) {
            if (face.indices.size() < 2) {
                continue;
            }

            for (size_t i = 0; i < face.indices.size(); ++i) {
                const uint32_t a = face.indices[i];
                const uint32_t b = face.indices[(i + 1) % face.indices.size()];
                if (a >= mesh.points.size() || b >= mesh.points.size()) {
                    continue;
                }
                DrawWorldLine(dc, editor, mesh.points[a], mesh.points[b], color, width);
            }
        }
    }
}

void DrawViewport(HDC dc, const EditorState& editor) {
    const RectI& viewport = editor.layout.viewport;
    Fill(dc, viewport, Rgb(64, 64, 64));
    Stroke(dc, viewport, editor.viewportActive ? Rgb(255, 140, 0) : Rgb(22, 22, 22));

    HRGN clip = CreateRectRgn(viewport.x + 1, viewport.y + 1, viewport.x + viewport.w - 1, viewport.y + viewport.h - 1);
    SelectClipRgn(dc, clip);

    DrawGridAndAxes(dc, editor);
    DrawMeshes(dc, editor);

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);

    RectI viewLabel{viewport.x + 10, viewport.y + 8, 154, 24};
    Fill(dc, viewLabel, Rgb(54, 54, 54));
    Stroke(dc, viewLabel, editor.viewMenuOpen ? Rgb(255, 148, 0) : Rgb(82, 82, 82));
    Text(dc, ViewModeName(editor.activeView) + L"  v", viewLabel.x + 7, viewLabel.y, viewLabel.w - 14, viewLabel.h, Rgb(255, 148, 0), editor.boldFont);

    const int axisX = viewport.x + 22;
    const int axisY = viewport.y + viewport.h - 42;
    DrawLine(dc, axisX, axisY, axisX, axisY - 38, Rgb(255, 148, 0), 2);
    DrawLine(dc, axisX, axisY, axisX + 38, axisY, Rgb(255, 148, 0), 2);
    Text(dc, L"Y", axisX - 6, axisY - 55, 22, 18, Rgb(255, 148, 0), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, L"X", axisX + 34, axisY - 2, 22, 18, Rgb(255, 148, 0), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    Fill(dc, {viewport.x + viewport.w - 316, viewport.y + 8, 300, 24}, Rgb(47, 47, 47));
    Stroke(dc, {viewport.x + viewport.w - 316, viewport.y + 8, 300, 24}, Rgb(88, 88, 88));
    Text(dc, L"RMB+WASD fly  Alt+LMB orbit  F focus", viewport.x + viewport.w - 308, viewport.y + 8, 284, 24, Rgb(188, 188, 188), editor.smallFont);
}

RectI ViewLabelRect(const EditorState& editor) {
    const RectI& viewport = editor.layout.viewport;
    return {viewport.x + 10, viewport.y + 8, 154, 24};
}

ViewMode ViewModeFromPopupIndex(int index) {
    switch (index) {
        case 0: return ViewMode::All;
        case 1: return ViewMode::Front;
        case 2: return ViewMode::Back;
        case 3: return ViewMode::Left;
        case 4: return ViewMode::Right;
        case 5: return ViewMode::Top;
        case 6: return ViewMode::Bottom;
        case 7: return ViewMode::Ortho;
        case 8: return ViewMode::Perspective;
        default: return ViewMode::Perspective;
    }
}

void DrawViewportViewPopup(HDC dc, const EditorState& editor) {
    if (!editor.viewMenuOpen) {
        return;
    }

    const RectI label = ViewLabelRect(editor);
    const int rowH = 23;
    const int count = 9;
    RectI menu{label.x, label.y + label.h + 2, 154, rowH * count + 8};
    Fill(dc, menu, Rgb(29, 31, 36));
    Stroke(dc, menu, Rgb(255, 148, 0));

    for (int i = 0; i < count; ++i) {
        const ViewMode mode = ViewModeFromPopupIndex(i);
        RectI row{menu.x + 4, menu.y + 4 + i * rowH, menu.w - 8, rowH};
        const bool active = mode == editor.activeView;
        Fill(dc, row, active ? Rgb(55, 48, 38) : Rgb(29, 31, 36));
        Text(dc, ViewModeName(mode), row.x + 8, row.y, row.w - 16, row.h, active ? Rgb(255, 148, 0) : Rgb(229, 232, 237), editor.smallFont);
    }
}

void DrawConsole(HDC dc, const EditorState& editor) {
    const RectI& panel = editor.layout.console;
    Fill(dc, panel, Rgb(21, 23, 28));
    Stroke(dc, panel, Rgb(68, 74, 84));
    DrawPanelHeader(dc, editor, panel, L"Console");

    int y = panel.y + 36;
    const int maxLines = std::max(1, (panel.h - 42) / 20);
    const int start = std::max(0, static_cast<int>(editor.consoleLines.size()) - maxLines);
    for (int i = start; i < static_cast<int>(editor.consoleLines.size()); ++i) {
        Text(dc, editor.consoleLines[static_cast<size_t>(i)], panel.x + 12, y, panel.w - 24, 20, Rgb(184, 194, 206), editor.smallFont);
        y += 20;
    }
}

void DrawStatus(HDC dc, const EditorState& editor) {
    const RectI& status = editor.layout.status;
    Fill(dc, status, Rgb(48, 48, 48));
    DrawLine(dc, status.x, status.y, status.x + status.w, status.y, Rgb(5, 5, 5));

    const wchar_t* modes[] = {L"Object editor", L"Figure editor", L"Sequence editor", L"Scene editor"};
    Text(dc, modes[editor.activeMode], status.x + 3, status.y, 180, status.h, Rgb(255, 148, 0), editor.smallFont);

    const wchar_t* dockTabs[] = {L"Explorer", L"Inspector", L"Console", L"Materials", L"Timeline"};
    int x = 250;
    for (int i = 0; i < 5; ++i) {
        RectI tab{x, status.y + 2, 82, status.h - 4};
        Stroke(dc, tab, Rgb(28, 28, 28));
        Text(dc, dockTabs[i], tab.x, tab.y, tab.w, tab.h, Rgb(150, 150, 150), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += tab.w + 4;
    }

    std::wstring right = L"Object: ";
    if (editor.selectedMesh >= 0 && editor.selectedMesh < static_cast<int>(editor.meshes.size())) {
        right += ToWide(editor.meshes[static_cast<size_t>(editor.selectedMesh)].objectName);
    } else {
        right += L"none";
    }
    Text(dc, right, status.w - 260, status.y, 252, status.h, Rgb(255, 148, 0), editor.smallFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void PaintEditor(HWND hwnd, EditorState& editor) {
    PAINTSTRUCT ps{};
    HDC windowDc = BeginPaint(hwnd, &ps);
    HDC dc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, editor.width, editor.height);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    Fill(dc, {0, 0, editor.width, editor.height}, Rgb(18, 20, 25));
    DrawTopBar(dc, editor);
    DrawToolBar(dc, editor);
    DrawViewport(dc, editor);
    DrawInspector(dc, editor);
    DrawConsole(dc, editor);
    DrawStatus(dc, editor);
    DrawViewportViewPopup(dc, editor);

    BitBlt(windowDc, 0, 0, editor.width, editor.height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

bool IsKeyDown(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

void TickCamera(EditorState& editor) {
    ViewportCameraInput input;
    input.viewportHovered = editor.viewportActive;
    input.rightMouseDown = editor.rightMouseDown;
    input.leftMouseDown = editor.leftMouseDown;
    input.middleMouseDown = editor.middleMouseDown;
    input.altDown = IsKeyDown(VK_MENU);
    input.shiftDown = IsKeyDown(VK_SHIFT);
    input.keyW = IsKeyDown('W');
    input.keyA = IsKeyDown('A');
    input.keyS = IsKeyDown('S');
    input.keyD = IsKeyDown('D');
    input.keyQ = IsKeyDown('Q');
    input.keyE = IsKeyDown('E');
    input.focusPressed = editor.focusPressed;
    input.mouseDeltaX = editor.accumulatedMouseDx;
    input.mouseDeltaY = editor.accumulatedMouseDy;
    input.wheelDelta = editor.accumulatedWheel;
    input.deltaSeconds = static_cast<float>(kFrameMillis) / 1000.0f;
    input.viewportWidth = editor.layout.viewport.w;
    input.viewportHeight = editor.layout.viewport.h;
    input.hasSelection = !editor.meshes.empty();
    input.selectionCenter = editor.selectionCenter;
    input.selectionRadius = editor.selectionRadius;

    editor.camera.Update(input);
    editor.accumulatedMouseDx = 0.0f;
    editor.accumulatedMouseDy = 0.0f;
    editor.accumulatedWheel = 0.0f;
    editor.focusPressed = false;
}

void CaptureViewportMouse(HWND hwnd, EditorState& editor, int x, int y) {
    editor.viewportActive = true;
    editor.lastMouse = POINT{x, y};
    SetCapture(hwnd);
    SetFocus(hwnd);
}

void ReleaseViewportMouse(EditorState& editor) {
    if (!editor.leftMouseDown && !editor.middleMouseDown && !editor.rightMouseDown) {
        ReleaseCapture();
    }
}

void ClickHierarchy(EditorState& editor, int x, int y) {
    const RectI& panel = editor.layout.leftPanel;
    if (!panel.Contains(x, y)) {
        return;
    }

    int rowY = panel.y + 66;
    for (size_t i = 0; i < editor.meshes.size(); ++i) {
        RectI row{panel.x + 8, rowY, panel.w - 16, 48};
        if (row.Contains(x, y)) {
            editor.selectedMesh = static_cast<int>(i);
            RecalculateSelectionBounds(editor);
            editor.camera.FocusOn(editor.selectionCenter, editor.selectionRadius);
            AddConsoleLine(editor, L"Selected mesh: " + ToWide(editor.meshes[i].meshName));
            InvalidateRect(editor.hwnd, nullptr, FALSE);
            return;
        }
        rowY += 54;
    }
}

void ClickToolRail(EditorState& editor, int x, int y) {
    const RectI& rail = editor.layout.leftPanel;
    if (!rail.Contains(x, y)) {
        return;
    }

    int scanY = rail.y + 2;
    int toolIndex = 0;
    const int groupCounts[] = {4, 4, 0, 10, 2, 9, 4, 4};
    for (int group = 0; group < 8; ++group) {
        scanY += 18;
        if (group == 2) {
            scanY += 23;
            continue;
        }

        const int count = groupCounts[group];
        for (int i = 0; i < count; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            RectI button{rail.x + 4 + col * 27, scanY + row * 27, 22, 22};
            if (button.Contains(x, y)) {
                editor.activeTool = toolIndex;
                AddConsoleLine(editor, L"Tool selected.");
                InvalidateRect(editor.hwnd, nullptr, FALSE);
                return;
            }
            ++toolIndex;
        }

        scanY += ((count + 1) / 2) * 27 + 6;
    }
}

void SetEditorMode(EditorState& editor, int mode) {
    editor.activeMode = std::clamp(mode, 0, 3);
    const int pages[] = {3, 4, 5, 6};
    editor.propertyPage = pages[editor.activeMode];
}

void ClickPropertyDeck(EditorState& editor, int x, int y) {
    const RectI& panel = editor.layout.rightPanel;
    if (!panel.Contains(x, y)) {
        return;
    }

    int tabX = panel.x + 8;
    int tabY = panel.y + 36;
    for (int i = 0; i < 8; ++i) {
        const int tabW = i == 5 ? 72 : 58;
        RectI tab{tabX, tabY, tabW, 22};
        if (tab.Contains(x, y)) {
            editor.propertyPage = i;
            AddConsoleLine(editor, L"Property page selected.");
            InvalidateRect(editor.hwnd, nullptr, FALSE);
            return;
        }

        tabX += tabW + 4;
        if (tabX + 64 > panel.x + panel.w - 8) {
            tabX = panel.x + 8;
            tabY += 26;
        }
    }
}

bool ClickViewportViewSelector(EditorState& editor, int x, int y) {
    const RectI label = ViewLabelRect(editor);
    if (label.Contains(x, y)) {
        editor.viewMenuOpen = !editor.viewMenuOpen;
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return true;
    }

    if (!editor.viewMenuOpen) {
        return false;
    }

    const int rowH = 23;
    const int count = 9;
    RectI menu{label.x, label.y + label.h + 2, 154, rowH * count + 8};
    if (menu.Contains(x, y)) {
        const int index = std::clamp((y - menu.y - 4) / rowH, 0, count - 1);
        SetViewportView(editor, ViewModeFromPopupIndex(index));
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return true;
    }

    editor.viewMenuOpen = false;
    InvalidateRect(editor.hwnd, nullptr, FALSE);
    return false;
}

void ClickTopBar(EditorState& editor, int x, int y) {
    if (!editor.layout.topBar.Contains(x, y)) {
        return;
    }

    const int barWidth = editor.layout.topBar.w;
    int tabX = std::max(390, barWidth - 238);
    for (int i = 0; i < 4; ++i) {
        const int tabW = i == 0 ? 50 : 62;
        RectI tab{tabX, editor.layout.topBar.y + 3, tabW, 22};
        if (tab.Contains(x, y)) {
            SetEditorMode(editor, i);
            AddConsoleLine(editor, L"Switched editor mode.");
            InvalidateRect(editor.hwnd, nullptr, FALSE);
            return;
        }
        tabX += tabW + 4;
    }

    int toolX = 4;
    for (int i = 0; i < 15; ++i) {
        RectI button{toolX, editor.layout.topBar.y + 4, 20, 20};
        if (button.Contains(x, y)) {
            editor.activeTool = i;
            AddConsoleLine(editor, L"Command strip tool selected.");
            InvalidateRect(editor.hwnd, nullptr, FALSE);
            return;
        }
        toolX += 23;
        if (i == 5 || i == 9) {
            toolX += 8;
        }
    }
}

HMENU CreateAnim8orXMenu() {
    HMENU menu = CreateMenu();

    struct MenuSpec {
        const wchar_t* name;
        const wchar_t* items[12];
    };

    const MenuSpec specs[] = {
        {L"File", {L"New", L"Open .an8...", L"Save", L"Save As...", L"Import", L"Export", L"Preferences", L"Exit", nullptr}},
        {L"Edit", {L"Undo", L"Redo", L"Cut", L"Copy", L"Paste", L"Delete", L"Select All", L"Preferences", nullptr}},
        {L"Mode", {L"Object", L"Figure", L"Sequence", L"Scene", nullptr}},
        {L"Object", {L"New Mesh", L"Convert to Mesh", L"Join Solids", L"Subdivide Faces", L"Extrude", L"Lathe", L"Mirror", L"Smooth", nullptr}},
        {L"Options", {L"Grid", L"Snapping", L"Show Axis", L"Show Normals", L"Backface Culling", L"Theme", nullptr}},
        {L"View", {L"All", L"Front", L"Back", L"Left", L"Right", L"Top", L"Bottom", L"Ortho", L"Perspective", L"Frame Selection", nullptr}},
        {L"Build", {L"Add Cube", L"Add Sphere", L"Add Cylinder", L"Add Cone", L"Add Text", L"Add Bone", L"Add Camera", L"Add Light", nullptr}},
        {L"Scripts", {L"Run Script...", L"Script Console", L"Reload Scripts", nullptr}},
        {L"Render", {L"Preview Render", L"Render Settings", L"Materials", L"Lights", nullptr}},
        {L"Window", {L"Explorer", L"Inspector", L"Console", L"Materials", L"Timeline", L"Reset Layout", nullptr}},
        {L"About", {L"About Anim8orX", L"License", L"GitHub", nullptr}}
    };

    for (const MenuSpec& spec : specs) {
        HMENU popup = CreatePopupMenu();
        for (int i = 0; i < 12 && spec.items[i] != nullptr; ++i) {
            UINT_PTR id = 2000 + static_cast<UINT_PTR>((spec.name[0] << 4) + i);
            if (wcscmp(spec.name, L"File") == 0) {
                if (i == 0) {
                    id = kMenuFileNew;
                } else if (i == 1) {
                    id = kMenuFileOpen;
                } else if (i == 7) {
                    id = kMenuFileExit;
                }
            } else if (wcscmp(spec.name, L"Mode") == 0) {
                id = i == 0 ? kMenuModeObject : i == 1 ? kMenuModeFigure : i == 2 ? kMenuModeSequence : kMenuModeScene;
            } else if (wcscmp(spec.name, L"View") == 0) {
                id = kMenuViewAll + i;
            }
            AppendMenuW(popup, MF_STRING, id, spec.items[i]);
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(popup), spec.name);
    }

    return menu;
}

LRESULT CALLBACK EditorWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    EditorState& editor = g_editor;

    switch (message) {
        case WM_CREATE:
            editor.hwnd = hwnd;
            editor.font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
            editor.boldFont = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                          DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
            editor.smallFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                           DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
            CalculateLayout(editor);
            DragAcceptFiles(hwnd, TRUE);
            SetTimer(hwnd, kFrameTimerId, kFrameMillis, nullptr);
            return 0;

        case WM_SIZE:
            editor.width = std::max(640, static_cast<int>(LOWORD(lParam)));
            editor.height = std::max(480, static_cast<int>(HIWORD(lParam)));
            CalculateLayout(editor);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            if (wParam == kFrameTimerId) {
                TickCamera(editor);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_PAINT:
            PaintEditor(hwnd, editor);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kMenuFileNew:
                    NewDefaultDocument(editor);
                    break;
                case kMenuFileOpen:
                    OpenAn8FromDialog(editor);
                    break;
                case kMenuFileExit:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    break;
                case kMenuModeObject:
                    SetEditorMode(editor, 0);
                    break;
                case kMenuModeFigure:
                    SetEditorMode(editor, 1);
                    break;
                case kMenuModeSequence:
                    SetEditorMode(editor, 2);
                    break;
                case kMenuModeScene:
                    SetEditorMode(editor, 3);
                    break;
                case kMenuViewAll:
                    SetViewportView(editor, ViewMode::All);
                    break;
                case kMenuViewFront:
                    SetViewportView(editor, ViewMode::Front);
                    break;
                case kMenuViewBack:
                    SetViewportView(editor, ViewMode::Back);
                    break;
                case kMenuViewLeft:
                    SetViewportView(editor, ViewMode::Left);
                    break;
                case kMenuViewRight:
                    SetViewportView(editor, ViewMode::Right);
                    break;
                case kMenuViewTop:
                    SetViewportView(editor, ViewMode::Top);
                    break;
                case kMenuViewBottom:
                    SetViewportView(editor, ViewMode::Bottom);
                    break;
                case kMenuViewOrtho:
                    SetViewportView(editor, ViewMode::Ortho);
                    break;
                case kMenuViewPerspective:
                    SetViewportView(editor, ViewMode::Perspective);
                    break;
                default:
                    AddConsoleLine(editor, L"Menu command selected. Implementation pending.");
                    break;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_DROPFILES:
            HandleDroppedFiles(editor, reinterpret_cast<HDROP>(wParam));
            return 0;

        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (ClickViewportViewSelector(editor, x, y)) {
                return 0;
            }

            if (editor.layout.viewport.Contains(x, y)) {
                editor.leftMouseDown = true;
                CaptureViewportMouse(hwnd, editor, x, y);
            } else {
                ClickTopBar(editor, x, y);
                ClickToolRail(editor, x, y);
                ClickPropertyDeck(editor, x, y);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            editor.leftMouseDown = false;
            ReleaseViewportMouse(editor);
            return 0;

        case WM_RBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (editor.layout.viewport.Contains(x, y)) {
                editor.rightMouseDown = true;
                CaptureViewportMouse(hwnd, editor, x, y);
            }
            return 0;
        }

        case WM_RBUTTONUP:
            editor.rightMouseDown = false;
            ReleaseViewportMouse(editor);
            return 0;

        case WM_MBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (editor.layout.viewport.Contains(x, y)) {
                editor.middleMouseDown = true;
                CaptureViewportMouse(hwnd, editor, x, y);
            }
            return 0;
        }

        case WM_MBUTTONUP:
            editor.middleMouseDown = false;
            ReleaseViewportMouse(editor);
            return 0;

        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (editor.leftMouseDown || editor.middleMouseDown || editor.rightMouseDown) {
                editor.accumulatedMouseDx += static_cast<float>(x - editor.lastMouse.x);
                editor.accumulatedMouseDy += static_cast<float>(y - editor.lastMouse.y);
            }
            editor.viewportActive = editor.layout.viewport.Contains(x, y) || editor.leftMouseDown || editor.middleMouseDown || editor.rightMouseDown;
            editor.lastMouse = POINT{x, y};
            return 0;
        }

        case WM_MOUSEWHEEL: {
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &screenPoint);
            if (editor.layout.viewport.Contains(screenPoint.x, screenPoint.y)) {
                editor.viewportActive = true;
                editor.accumulatedWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == 'F') {
                editor.focusPressed = true;
            } else if (wParam == VK_ESCAPE) {
                editor.viewportActive = false;
                editor.leftMouseDown = false;
                editor.middleMouseDown = false;
                editor.rightMouseDown = false;
                ReleaseCapture();
            }
            return 0;

        case WM_DESTROY:
            DragAcceptFiles(hwnd, FALSE);
            KillTimer(hwnd, kFrameTimerId);
            if (editor.font != nullptr) {
                DeleteObject(editor.font);
            }
            if (editor.boldFont != nullptr) {
                DeleteObject(editor.boldFont);
            }
            if (editor.smallFont != nullptr) {
                DeleteObject(editor.smallFont);
            }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int RunSmokeTest(const std::filesystem::path& path) {
    EditorState testEditor;
    LoadDocument(testEditor, path);
    LoadLogo(testEditor);
    return testEditor.meshes.empty() || testEditor.logoImage == nullptr ? 1 : 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        return 1;
    }

    const std::vector<std::wstring> args = CommandLineArguments();

    bool smokeTest = false;
    std::filesystem::path requestedPath;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == L"--smoke-test") {
            smokeTest = true;
        } else {
            requestedPath = args[i];
        }
    }

    if (smokeTest) {
        const int result = RunSmokeTest(requestedPath);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return result;
    }

    LoadDocument(g_editor, requestedPath);
    LoadLogo(g_editor);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = EditorWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = g_editor.logoIcon != nullptr ? g_editor.logoIcon : LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = g_editor.logoIcon != nullptr ? g_editor.logoIcon : LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&windowClass)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    RECT windowRect{0, 0, 1320, 860};
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        L"Anim8orX",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (hwnd == nullptr) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    SetMenu(hwnd, CreateAnim8orXMenu());
    RefreshWindowTitle(g_editor);
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_editor.logoImage.reset();
    if (g_editor.logoIcon != nullptr) {
        DestroyIcon(g_editor.logoIcon);
        g_editor.logoIcon = nullptr;
    }
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return static_cast<int>(message.wParam);
}
