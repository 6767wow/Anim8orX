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

#include <Anim8orX/Import/An8Parser.hpp>
#include <Anim8orX/Viewport/Camera.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
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
    POINT lastMouse = {};
    float accumulatedMouseDx = 0.0f;
    float accumulatedMouseDy = 0.0f;
    float accumulatedWheel = 0.0f;

    Vec3 selectionCenter = {0.0f, 0.0f, 0.0f};
    float selectionRadius = 1.5f;

    int activeMode = 0;
    int selectedMesh = 0;
};

EditorState g_editor;

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
    editor.camera.FocusOn(editor.selectionCenter, editor.selectionRadius);
    return !editor.meshes.empty();
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
    const int top = 38;
    const int toolbar = 42;
    const int console = std::max(116, std::min(162, editor.height / 5));
    const int status = 24;
    const int left = std::max(230, std::min(290, editor.width / 5));
    const int right = std::max(280, std::min(340, editor.width / 4));

    editor.layout.topBar = {0, 0, editor.width, top};
    editor.layout.leftPanel = {0, top, left, editor.height - top - console - status};
    editor.layout.rightPanel = {editor.width - right, top, right, editor.height - top - console - status};
    editor.layout.toolBar = {left, top, editor.width - left - right, toolbar};
    editor.layout.viewport = {left, top + toolbar, editor.width - left - right, editor.height - top - toolbar - console - status};
    editor.layout.console = {0, editor.height - console - status, editor.width, console};
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

void DrawTopBar(HDC dc, const EditorState& editor) {
    const RectI& bar = editor.layout.topBar;
    Fill(dc, bar, Rgb(20, 22, 27));
    Text(dc, L"Anim8orX", 14, 0, 128, bar.h, Rgb(245, 247, 250), editor.boldFont);

    const wchar_t* modes[] = {L"Object", L"Figure", L"Sequence", L"Scene"};
    int x = 132;
    for (int i = 0; i < 4; ++i) {
        const int tabW = 108;
        RectI tab{x, 6, tabW, 26};
        Fill(dc, tab, i == editor.activeMode ? Rgb(45, 79, 86) : Rgb(32, 35, 42));
        Stroke(dc, tab, i == editor.activeMode ? Rgb(87, 176, 186) : Rgb(68, 74, 84));
        Text(dc, modes[i], tab.x, tab.y, tab.w, tab.h, Rgb(235, 240, 244), editor.font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += tabW + 8;
    }

    Text(dc, L"Native editor shell", bar.w - 226, 0, 208, bar.h, Rgb(157, 166, 178), editor.smallFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void DrawToolBar(HDC dc, const EditorState& editor) {
    const RectI& bar = editor.layout.toolBar;
    Fill(dc, bar, Rgb(29, 31, 37));
    Stroke(dc, bar, Rgb(68, 74, 84));

    const wchar_t* workspaceTabs[] = {L"Viewport", L"Hierarchy", L"Inspector", L"Console"};
    int x = bar.x + 10;
    for (int i = 0; i < 4; ++i) {
        RectI tab{x, bar.y + 7, 92, 28};
        Fill(dc, tab, i == 0 ? Rgb(45, 79, 86) : Rgb(36, 39, 46));
        Stroke(dc, tab, i == 0 ? Rgb(87, 176, 186) : Rgb(67, 73, 83));
        Text(dc, workspaceTabs[i], tab.x, tab.y, tab.w, tab.h, Rgb(228, 233, 238), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += tab.w + 7;
    }

    DrawLine(dc, x + 4, bar.y + 9, x + 4, bar.y + bar.h - 9, Rgb(70, 76, 86));
    x += 18;

    const wchar_t* tools[] = {L"Select", L"Move", L"Rotate", L"Scale", L"Extrude", L"Lathe", L"Subdivide"};
    for (const wchar_t* tool : tools) {
        RectI button{x, bar.y + 8, 76, 26};
        Fill(dc, button, wcscmp(tool, L"Select") == 0 ? Rgb(64, 73, 58) : Rgb(42, 45, 52));
        Stroke(dc, button, Rgb(78, 85, 96));
        Text(dc, tool, button.x, button.y, button.w, button.h, Rgb(228, 232, 237), editor.smallFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += button.w + 8;
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
    Text(dc, label, x, y, 96, 22, Rgb(171, 181, 192), editor.smallFont);
    RectI field{x + 100, y + 2, 164, 20};
    Fill(dc, field, Rgb(18, 20, 24));
    Stroke(dc, field, Rgb(64, 70, 80));
    Text(dc, value, field.x + 7, field.y, field.w - 14, field.h, Rgb(230, 235, 240), editor.smallFont);
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
    Fill(dc, panel, Rgb(26, 28, 34));
    Stroke(dc, panel, Rgb(68, 74, 84));
    DrawPanelHeader(dc, editor, panel, L"Inspector");

    int y = panel.y + 42;
    const MeshView* mesh = editor.selectedMesh >= 0 && editor.selectedMesh < static_cast<int>(editor.meshes.size())
        ? &editor.meshes[static_cast<size_t>(editor.selectedMesh)]
        : nullptr;

    Text(dc, L"Transform", panel.x + 12, y, panel.w - 24, 24, Rgb(236, 240, 244), editor.boldFont);
    y += 30;
    DrawInspectorRow(dc, editor, panel.x + 12, y, L"Position", L"0.00, 0.00, 0.00");
    y += 26;
    DrawInspectorRow(dc, editor, panel.x + 12, y, L"Rotation", L"0.00, 0.00, 0.00");
    y += 26;
    DrawInspectorRow(dc, editor, panel.x + 12, y, L"Scale", L"1.00, 1.00, 1.00");
    y += 42;

    Text(dc, L"Mesh Component", panel.x + 12, y, panel.w - 24, 24, Rgb(236, 240, 244), editor.boldFont);
    y += 30;

    if (mesh != nullptr) {
        DrawInspectorRow(dc, editor, panel.x + 12, y, L"Object", ToWide(mesh->objectName));
        y += 26;
        DrawInspectorRow(dc, editor, panel.x + 12, y, L"Mesh", ToWide(mesh->meshName));
        y += 26;
        DrawInspectorRow(dc, editor, panel.x + 12, y, L"Points", std::to_wstring(mesh->points.size()));
        y += 26;
        DrawInspectorRow(dc, editor, panel.x + 12, y, L"Faces", std::to_wstring(mesh->faces.size()));
        y += 26;
        DrawInspectorRow(dc, editor, panel.x + 12, y, L"Triangles", std::to_wstring(anim8orx::BuildTriangleGeometry({mesh->meshName, mesh->points, mesh->faces}).indices.size() / 3));
    } else {
        Text(dc, L"No selection", panel.x + 12, y, panel.w - 24, 22, Rgb(166, 175, 187), editor.font);
    }

    y = panel.y + panel.h - 108;
    Text(dc, L"Camera", panel.x + 12, y, panel.w - 24, 24, Rgb(236, 240, 244), editor.boldFont);
    y += 30;
    DrawInspectorRow(dc, editor, panel.x + 12, y, L"Yaw", FormatFloat(editor.camera.yawRadians / anim8orx::AX_DEG_TO_RAD));
    y += 26;
    DrawInspectorRow(dc, editor, panel.x + 12, y, L"Pitch", FormatFloat(editor.camera.pitchRadians / anim8orx::AX_DEG_TO_RAD));
}

void DrawGridAndAxes(HDC dc, const EditorState& editor) {
    for (int i = -10; i <= 10; ++i) {
        const COLORREF gridColor = i == 0 ? Rgb(73, 78, 88) : Rgb(45, 49, 57);
        DrawWorldLine(dc, editor, {static_cast<float>(i), 0.0f, -10.0f}, {static_cast<float>(i), 0.0f, 10.0f}, gridColor);
        DrawWorldLine(dc, editor, {-10.0f, 0.0f, static_cast<float>(i)}, {10.0f, 0.0f, static_cast<float>(i)}, gridColor);
    }

    DrawWorldLine(dc, editor, {-10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, Rgb(176, 82, 82), 2);
    DrawWorldLine(dc, editor, {0.0f, 0.0f, -10.0f}, {0.0f, 0.0f, 10.0f}, Rgb(82, 140, 92), 2);
    DrawWorldLine(dc, editor, {0.0f, -1.0f, 0.0f}, {0.0f, 4.0f, 0.0f}, Rgb(78, 130, 202), 2);
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
    Fill(dc, viewport, Rgb(15, 17, 21));
    Stroke(dc, viewport, editor.viewportActive ? Rgb(89, 170, 184) : Rgb(68, 74, 84));

    HRGN clip = CreateRectRgn(viewport.x + 1, viewport.y + 1, viewport.x + viewport.w - 1, viewport.y + viewport.h - 1);
    SelectClipRgn(dc, clip);

    DrawGridAndAxes(dc, editor);
    DrawMeshes(dc, editor);

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);

    Fill(dc, {viewport.x + 10, viewport.y + 10, 210, 28}, Rgb(22, 24, 29));
    Stroke(dc, {viewport.x + 10, viewport.y + 10, 210, 28}, Rgb(65, 71, 81));
    Text(dc, L"Perspective Viewport", viewport.x + 20, viewport.y + 10, 190, 28, Rgb(228, 233, 238), editor.smallFont);
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
    Fill(dc, status, Rgb(32, 35, 42));
    std::wstring text = L"Mode: ";
    const wchar_t* modes[] = {L"Object", L"Figure", L"Sequence", L"Scene"};
    text += modes[editor.activeMode];
    text += L"    Meshes: " + std::to_wstring(editor.meshes.size());
    text += L"    Camera: Unity-style";
    Text(dc, text, status.x + 12, status.y, status.w - 24, status.h, Rgb(203, 211, 220), editor.smallFont);
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
    DrawHierarchy(dc, editor);
    DrawViewport(dc, editor);
    DrawInspector(dc, editor);
    DrawConsole(dc, editor);
    DrawStatus(dc, editor);

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

void ClickTopBar(EditorState& editor, int x, int y) {
    if (!editor.layout.topBar.Contains(x, y)) {
        return;
    }

    int tabX = 132;
    for (int i = 0; i < 4; ++i) {
        RectI tab{tabX, 6, 108, 26};
        if (tab.Contains(x, y)) {
            editor.activeMode = i;
            AddConsoleLine(editor, L"Switched editor mode.");
            InvalidateRect(editor.hwnd, nullptr, FALSE);
            return;
        }
        tabX += 116;
    }
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

        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (editor.layout.viewport.Contains(x, y)) {
                editor.leftMouseDown = true;
                CaptureViewportMouse(hwnd, editor, x, y);
            } else {
                ClickTopBar(editor, x, y);
                ClickHierarchy(editor, x, y);
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
    return testEditor.meshes.empty() ? 1 : 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
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
        return RunSmokeTest(requestedPath);
    }

    LoadDocument(g_editor, requestedPath);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = EditorWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;

    if (!RegisterClassExW(&windowClass)) {
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
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
