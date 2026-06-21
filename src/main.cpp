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
#include <fstream>
#include <filesystem>
#include <iomanip>
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
constexpr int kMenuFileSave = 1203;
constexpr int kMenuFileSaveAs = 1204;
constexpr int kMenuFileImport = 1205;
constexpr int kMenuFileExport = 1206;
constexpr int kMenuEditDelete = 1210;
constexpr int kMenuObjectSubdivide = 1220;
constexpr int kMenuObjectExtrude = 1221;
constexpr int kMenuObjectLathe = 1222;
constexpr int kMenuObjectMirror = 1223;
constexpr int kMenuObjectInset = 1224;
constexpr int kMenuOptionGrid = 1240;
constexpr int kMenuOptionSnapping = 1241;
constexpr int kMenuOptionAxis = 1242;
constexpr int kMenuOptionNormals = 1243;
constexpr int kMenuOptionBackface = 1244;
constexpr int kMenuOptionPreferences = 1245;
constexpr int kMenuBuildCube = 1230;
constexpr int kMenuBuildSphere = 1231;
constexpr int kMenuBuildCylinder = 1232;
constexpr int kMenuBuildCone = 1233;
constexpr int kMenuBuildCamera = 1234;
constexpr int kMenuBuildLight = 1235;
constexpr int kMenuBuildTorus = 1236;
constexpr int kMenuBuildBone = 1237;
constexpr int kMenuBuildText = 1238;
constexpr int kMenuViewAll = 1100;
constexpr int kMenuViewFront = 1101;
constexpr int kMenuViewBack = 1102;
constexpr int kMenuViewLeft = 1103;
constexpr int kMenuViewRight = 1104;
constexpr int kMenuViewTop = 1105;
constexpr int kMenuViewBottom = 1106;
constexpr int kMenuViewOrtho = 1107;
constexpr int kMenuViewPerspective = 1108;
constexpr int kMenuViewFrameSelection = 1109;

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
    HDC backBufferDc = nullptr;
    HBITMAP backBufferBitmap = nullptr;
    HGDIOBJ backBufferOldBitmap = nullptr;
    int backBufferWidth = 0;
    int backBufferHeight = 0;
    bool backBufferValid = false;

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
    bool frameTimerActive = false;
    POINT lastMouse = {};
    float accumulatedMouseDx = 0.0f;
    float accumulatedMouseDy = 0.0f;
    float accumulatedWheel = 0.0f;

    Vec3 selectionCenter = {0.0f, 0.0f, 0.0f};
    float selectionRadius = 1.5f;
    Vec3 projectionForward = {0.0f, 0.0f, -1.0f};
    Vec3 projectionRight = {1.0f, 0.0f, 0.0f};
    Vec3 projectionUp = {0.0f, 1.0f, 0.0f};
    float projectionAspect = 1.0f;
    float projectionTanHalfFov = 0.57735026f;

    bool showGrid = true;
    bool showAxes = true;
    bool showNormals = false;
    bool gridSnap = true;
    bool backfaceCulling = false;

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

An8Face MakeFace(std::initializer_list<uint32_t> indices) {
    An8Face face;
    face.indices.assign(indices.begin(), indices.end());
    return face;
}

An8Mesh MakeCubeMesh(const std::string& name, float size, Vec3 center = {0.0f, 0.0f, 0.0f}) {
    const float h = size * 0.5f;
    An8Mesh mesh;
    mesh.name = name;
    mesh.points = {
        {center.x - h, center.y - h, center.z - h},
        {center.x + h, center.y - h, center.z - h},
        {center.x + h, center.y + h, center.z - h},
        {center.x - h, center.y + h, center.z - h},
        {center.x - h, center.y - h, center.z + h},
        {center.x + h, center.y - h, center.z + h},
        {center.x + h, center.y + h, center.z + h},
        {center.x - h, center.y + h, center.z + h}
    };
    mesh.faces = {
        MakeFace({0, 1, 2, 3}),
        MakeFace({4, 7, 6, 5}),
        MakeFace({0, 4, 5, 1}),
        MakeFace({1, 5, 6, 2}),
        MakeFace({2, 6, 7, 3}),
        MakeFace({3, 7, 4, 0})
    };
    return mesh;
}

An8Mesh MakeCylinderMesh(const std::string& name, float topRadius, float bottomRadius, float height, int segments) {
    An8Mesh mesh;
    mesh.name = name;
    segments = std::max(3, segments);
    const float half = height * 0.5f;

    for (int i = 0; i < segments; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * anim8orx::AX_PI;
        mesh.points.push_back({std::cos(a) * bottomRadius, -half, std::sin(a) * bottomRadius});
    }
    for (int i = 0; i < segments; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * anim8orx::AX_PI;
        mesh.points.push_back({std::cos(a) * topRadius, half, std::sin(a) * topRadius});
    }

    for (int i = 0; i < segments; ++i) {
        const uint32_t b0 = static_cast<uint32_t>(i);
        const uint32_t b1 = static_cast<uint32_t>((i + 1) % segments);
        const uint32_t t1 = static_cast<uint32_t>(segments + ((i + 1) % segments));
        const uint32_t t0 = static_cast<uint32_t>(segments + i);
        mesh.faces.push_back(MakeFace({b0, b1, t1, t0}));
    }

    An8Face bottom;
    An8Face top;
    for (int i = 0; i < segments; ++i) {
        bottom.indices.push_back(static_cast<uint32_t>(segments - 1 - i));
        top.indices.push_back(static_cast<uint32_t>(segments + i));
    }
    mesh.faces.push_back(std::move(bottom));
    mesh.faces.push_back(std::move(top));
    return mesh;
}

An8Mesh MakeSphereMesh(const std::string& name, float radius, int rings, int segments) {
    An8Mesh mesh;
    mesh.name = name;
    rings = std::max(3, rings);
    segments = std::max(6, segments);

    mesh.points.push_back({0.0f, radius, 0.0f});
    for (int r = 1; r < rings; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(rings);
        const float phi = v * anim8orx::AX_PI;
        const float y = std::cos(phi) * radius;
        const float ringRadius = std::sin(phi) * radius;
        for (int s = 0; s < segments; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(segments);
            const float theta = u * 2.0f * anim8orx::AX_PI;
            mesh.points.push_back({std::cos(theta) * ringRadius, y, std::sin(theta) * ringRadius});
        }
    }
    const uint32_t bottomIndex = static_cast<uint32_t>(mesh.points.size());
    mesh.points.push_back({0.0f, -radius, 0.0f});

    for (int s = 0; s < segments; ++s) {
        mesh.faces.push_back(MakeFace({0, static_cast<uint32_t>(1 + s), static_cast<uint32_t>(1 + ((s + 1) % segments))}));
    }

    for (int r = 0; r < rings - 2; ++r) {
        const uint32_t row0 = static_cast<uint32_t>(1 + r * segments);
        const uint32_t row1 = static_cast<uint32_t>(1 + (r + 1) * segments);
        for (int s = 0; s < segments; ++s) {
            mesh.faces.push_back(MakeFace({
                row0 + static_cast<uint32_t>(s),
                row0 + static_cast<uint32_t>((s + 1) % segments),
                row1 + static_cast<uint32_t>((s + 1) % segments),
                row1 + static_cast<uint32_t>(s)
            }));
        }
    }

    const uint32_t lastRow = static_cast<uint32_t>(1 + (rings - 2) * segments);
    for (int s = 0; s < segments; ++s) {
        mesh.faces.push_back(MakeFace({lastRow + static_cast<uint32_t>((s + 1) % segments), lastRow + static_cast<uint32_t>(s), bottomIndex}));
    }
    return mesh;
}

An8Mesh MakeTorusMesh(const std::string& name, float outerRadius, float tubeRadius, int rings, int segments) {
    An8Mesh mesh;
    mesh.name = name;
    outerRadius = std::max(0.05f, outerRadius);
    tubeRadius = std::max(0.01f, tubeRadius);
    rings = std::max(3, rings);
    segments = std::max(6, segments);

    for (int r = 0; r < rings; ++r) {
        const float u = (static_cast<float>(r) / static_cast<float>(rings)) * 2.0f * anim8orx::AX_PI;
        const float cu = std::cos(u);
        const float su = std::sin(u);
        for (int s = 0; s < segments; ++s) {
            const float v = (static_cast<float>(s) / static_cast<float>(segments)) * 2.0f * anim8orx::AX_PI;
            const float cv = std::cos(v);
            const float sv = std::sin(v);
            const float radius = outerRadius + tubeRadius * cv;
            mesh.points.push_back({radius * cu, tubeRadius * sv, radius * su});
        }
    }

    for (int r = 0; r < rings; ++r) {
        const uint32_t row0 = static_cast<uint32_t>(r * segments);
        const uint32_t row1 = static_cast<uint32_t>(((r + 1) % rings) * segments);
        for (int s = 0; s < segments; ++s) {
            mesh.faces.push_back(MakeFace({
                row0 + static_cast<uint32_t>(s),
                row1 + static_cast<uint32_t>(s),
                row1 + static_cast<uint32_t>((s + 1) % segments),
                row0 + static_cast<uint32_t>((s + 1) % segments)
            }));
        }
    }

    return mesh;
}

An8Mesh MakeCameraHelperMesh() {
    An8Mesh mesh;
    mesh.name = "Scene_Camera";
    mesh.points = {
        {0.0f, 0.0f, 0.0f},
        {-0.8f, -0.45f, -1.2f},
        {0.8f, -0.45f, -1.2f},
        {0.8f, 0.45f, -1.2f},
        {-0.8f, 0.45f, -1.2f},
        {-0.25f, -0.2f, 0.35f},
        {0.25f, -0.2f, 0.35f},
        {0.25f, 0.2f, 0.35f},
        {-0.25f, 0.2f, 0.35f}
    };
    mesh.faces = {
        MakeFace({0, 1, 2}),
        MakeFace({0, 2, 3}),
        MakeFace({0, 3, 4}),
        MakeFace({0, 4, 1}),
        MakeFace({5, 6, 7, 8})
    };
    return mesh;
}

An8Mesh MakeLightHelperMesh() {
    An8Mesh mesh;
    mesh.name = "Scene_Light";
    mesh.points = {
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.7f, 0.0f},
        {0.7f, 0.0f, 0.0f},
        {0.0f, -0.7f, 0.0f},
        {-0.7f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.7f},
        {0.0f, 0.0f, -0.7f}
    };
    mesh.faces = {
        MakeFace({1, 2, 3, 4}),
        MakeFace({5, 1, 6, 3}),
        MakeFace({5, 2, 6, 4})
    };
    return mesh;
}

An8Mesh MakeBoneHelperMesh() {
    An8Mesh mesh;
    mesh.name = "Figure_Bone";
    mesh.points = {
        {0.0f, 0.0f, 0.0f},
        {-0.22f, 0.22f, 0.0f},
        {0.0f, 0.0f, 0.22f},
        {0.22f, 0.22f, 0.0f},
        {0.0f, 0.0f, -0.22f},
        {0.0f, 1.6f, 0.0f}
    };
    mesh.faces = {
        MakeFace({0, 1, 2}),
        MakeFace({0, 2, 3}),
        MakeFace({0, 3, 4}),
        MakeFace({0, 4, 1}),
        MakeFace({5, 2, 1}),
        MakeFace({5, 3, 2}),
        MakeFace({5, 4, 3}),
        MakeFace({5, 1, 4})
    };
    return mesh;
}

An8Mesh MakeTextHelperMesh() {
    An8Mesh mesh;
    mesh.name = "Text_Spline_Helper";
    mesh.points = {
        {-1.2f, -0.35f, 0.0f},
        {-0.8f, 0.7f, 0.0f},
        {-0.4f, -0.35f, 0.0f},
        {-0.65f, 0.05f, 0.0f},
        {-0.95f, 0.05f, 0.0f},
        {0.05f, -0.35f, 0.0f},
        {0.05f, 0.7f, 0.0f},
        {0.65f, 0.7f, 0.0f},
        {0.65f, 0.45f, 0.0f},
        {0.32f, 0.45f, 0.0f},
        {0.32f, -0.35f, 0.0f}
    };
    mesh.faces = {
        MakeFace({0, 1, 2}),
        MakeFace({3, 4, 1}),
        MakeFace({5, 6, 7, 8, 9, 10})
    };
    return mesh;
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

An8Mesh* FindMeshByFlatIndex(An8Object& object, int targetIndex, int& currentIndex) {
    for (An8Mesh& mesh : object.meshes) {
        if (currentIndex == targetIndex) {
            return &mesh;
        }
        ++currentIndex;
    }

    for (An8Object& child : object.children) {
        if (An8Mesh* mesh = FindMeshByFlatIndex(child, targetIndex, currentIndex)) {
            return mesh;
        }
    }

    return nullptr;
}

An8Mesh* SelectedMesh(EditorState& editor) {
    if (editor.selectedMesh < 0) {
        return nullptr;
    }

    int current = 0;
    for (An8Object& object : editor.document.objects) {
        if (An8Mesh* mesh = FindMeshByFlatIndex(object, editor.selectedMesh, current)) {
            return mesh;
        }
    }

    return nullptr;
}

bool RemoveMeshByFlatIndex(An8Object& object, int targetIndex, int& currentIndex) {
    for (auto it = object.meshes.begin(); it != object.meshes.end(); ++it) {
        if (currentIndex == targetIndex) {
            object.meshes.erase(it);
            return true;
        }
        ++currentIndex;
    }

    for (An8Object& child : object.children) {
        if (RemoveMeshByFlatIndex(child, targetIndex, currentIndex)) {
            return true;
        }
    }

    return false;
}

void RefreshDocumentView(EditorState& editor, const std::wstring& message) {
    RebuildMeshViews(editor);
    RecalculateSelectionBounds(editor);
    editor.selectedMesh = editor.meshes.empty()
        ? -1
        : std::clamp(editor.selectedMesh, 0, static_cast<int>(editor.meshes.size()) - 1);
    editor.camera.FocusOn(editor.selectionCenter, editor.selectionRadius);
    AddConsoleLine(editor, message);
    InvalidateRect(editor.hwnd, nullptr, FALSE);
}

void AddMeshObject(EditorState& editor, const std::string& objectName, An8Mesh mesh) {
    An8Object object;
    object.name = objectName;
    object.meshes.push_back(std::move(mesh));
    editor.document.objects.push_back(std::move(object));
    editor.selectedMesh = static_cast<int>(editor.meshes.size());
    RefreshDocumentView(editor, L"Added " + ToWide(objectName) + L".");
}

void DeleteSelectedMesh(EditorState& editor) {
    if (editor.selectedMesh < 0) {
        AddConsoleLine(editor, L"Nothing selected to delete.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    int current = 0;
    for (An8Object& object : editor.document.objects) {
        if (RemoveMeshByFlatIndex(object, editor.selectedMesh, current)) {
            RefreshDocumentView(editor, L"Deleted selected mesh.");
            return;
        }
    }
}

void ExtrudeSelectedMesh(EditorState& editor) {
    An8Mesh* mesh = SelectedMesh(editor);
    if (mesh == nullptr || mesh->faces.empty()) {
        AddConsoleLine(editor, L"Extrude requires a selected mesh.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    const uint32_t offset = static_cast<uint32_t>(mesh->points.size());
    const float amount = std::max(editor.selectionRadius * 0.18f, 0.35f);
    const std::vector<An8Face> originalFaces = mesh->faces;

    for (const An8Vector3& point : std::vector<An8Vector3>(mesh->points.begin(), mesh->points.end())) {
        mesh->points.push_back({point.x, point.y, point.z + amount});
    }

    for (const An8Face& face : originalFaces) {
        if (face.indices.size() < 3) {
            continue;
        }

        An8Face top;
        for (auto it = face.indices.rbegin(); it != face.indices.rend(); ++it) {
            top.indices.push_back(offset + *it);
        }
        mesh->faces.push_back(std::move(top));

        for (size_t i = 0; i < face.indices.size(); ++i) {
            const uint32_t a = face.indices[i];
            const uint32_t b = face.indices[(i + 1) % face.indices.size()];
            mesh->faces.push_back(MakeFace({a, b, offset + b, offset + a}));
        }
    }

    RefreshDocumentView(editor, L"Extruded selected mesh.");
}

void SubdivideSelectedMesh(EditorState& editor) {
    An8Mesh* mesh = SelectedMesh(editor);
    if (mesh == nullptr || mesh->faces.empty()) {
        AddConsoleLine(editor, L"Subdivide requires a selected mesh.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    std::vector<An8Face> newFaces;
    for (const An8Face& face : mesh->faces) {
        if (face.indices.size() < 3) {
            continue;
        }

        An8Vector3 center{};
        int validCount = 0;
        for (uint32_t index : face.indices) {
            if (index < mesh->points.size()) {
                center.x += mesh->points[index].x;
                center.y += mesh->points[index].y;
                center.z += mesh->points[index].z;
                ++validCount;
            }
        }
        if (validCount == 0) {
            continue;
        }

        center.x /= static_cast<float>(validCount);
        center.y /= static_cast<float>(validCount);
        center.z /= static_cast<float>(validCount);
        const uint32_t centerIndex = static_cast<uint32_t>(mesh->points.size());
        mesh->points.push_back(center);

        for (size_t i = 0; i < face.indices.size(); ++i) {
            newFaces.push_back(MakeFace({face.indices[i], face.indices[(i + 1) % face.indices.size()], centerIndex}));
        }
    }

    mesh->faces = std::move(newFaces);
    RefreshDocumentView(editor, L"Subdivided selected mesh.");
}

void InsetSelectedMesh(EditorState& editor) {
    An8Mesh* mesh = SelectedMesh(editor);
    if (mesh == nullptr || mesh->faces.empty()) {
        AddConsoleLine(editor, L"Inset requires a selected mesh.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    const float amount = 0.72f;
    const std::vector<An8Face> originalFaces = mesh->faces;
    std::vector<An8Face> insetFaces;
    insetFaces.reserve(originalFaces.size() * 5);

    for (const An8Face& face : originalFaces) {
        if (face.indices.size() < 3) {
            continue;
        }

        An8Vector3 center{};
        int validCount = 0;
        for (uint32_t index : face.indices) {
            if (index < mesh->points.size()) {
                center.x += mesh->points[index].x;
                center.y += mesh->points[index].y;
                center.z += mesh->points[index].z;
                ++validCount;
            }
        }
        if (validCount < 3) {
            continue;
        }

        center.x /= static_cast<float>(validCount);
        center.y /= static_cast<float>(validCount);
        center.z /= static_cast<float>(validCount);

        std::vector<uint32_t> inner;
        inner.reserve(face.indices.size());
        for (uint32_t index : face.indices) {
            if (index >= mesh->points.size()) {
                continue;
            }

            const An8Vector3& source = mesh->points[index];
            An8Vector3 insetPoint;
            insetPoint.x = center.x + (source.x - center.x) * amount;
            insetPoint.y = center.y + (source.y - center.y) * amount;
            insetPoint.z = center.z + (source.z - center.z) * amount;
            inner.push_back(static_cast<uint32_t>(mesh->points.size()));
            mesh->points.push_back(insetPoint);
        }

        if (inner.size() != face.indices.size()) {
            continue;
        }

        An8Face innerFace;
        innerFace.indices = inner;
        insetFaces.push_back(std::move(innerFace));

        for (size_t i = 0; i < face.indices.size(); ++i) {
            const uint32_t outer0 = face.indices[i];
            const uint32_t outer1 = face.indices[(i + 1) % face.indices.size()];
            const uint32_t inner1 = inner[(i + 1) % inner.size()];
            const uint32_t inner0 = inner[i];
            insetFaces.push_back(MakeFace({outer0, outer1, inner1, inner0}));
        }
    }

    mesh->faces = std::move(insetFaces);
    RefreshDocumentView(editor, L"Inset selected mesh faces.");
}

void MirrorSelectedMesh(EditorState& editor) {
    An8Mesh* mesh = SelectedMesh(editor);
    if (mesh == nullptr || mesh->points.empty()) {
        AddConsoleLine(editor, L"Mirror requires a selected mesh.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    const uint32_t offset = static_cast<uint32_t>(mesh->points.size());
    const std::vector<An8Vector3> originalPoints = mesh->points;
    const std::vector<An8Face> originalFaces = mesh->faces;

    for (const An8Vector3& point : originalPoints) {
        mesh->points.push_back({-point.x, point.y, point.z});
    }

    for (const An8Face& face : originalFaces) {
        An8Face mirrored;
        for (auto it = face.indices.rbegin(); it != face.indices.rend(); ++it) {
            mirrored.indices.push_back(offset + *it);
        }
        mesh->faces.push_back(std::move(mirrored));
    }

    RefreshDocumentView(editor, L"Mirrored selected mesh across X.");
}

void LatheSelectedMesh(EditorState& editor) {
    An8Mesh* mesh = SelectedMesh(editor);
    if (mesh == nullptr) {
        AddConsoleLine(editor, L"Lathe requires a selected mesh.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return;
    }

    std::vector<An8Vector3> profile = mesh->points;
    if (profile.size() < 2) {
        profile = {
            {0.25f, -1.0f, 0.0f},
            {0.85f, -0.65f, 0.0f},
            {0.55f, 0.35f, 0.0f},
            {0.95f, 1.0f, 0.0f}
        };
    }

    std::sort(profile.begin(), profile.end(), [](const An8Vector3& a, const An8Vector3& b) {
        return a.y < b.y;
    });

    const int segments = 24;
    An8Mesh lathed;
    lathed.name = mesh->name + "_Lathe";

    for (int s = 0; s < segments; ++s) {
        const float a = (static_cast<float>(s) / static_cast<float>(segments)) * 2.0f * anim8orx::AX_PI;
        const float ca = std::cos(a);
        const float sa = std::sin(a);
        for (const An8Vector3& p : profile) {
            const float r = std::max(std::sqrt(p.x * p.x + p.z * p.z), 0.05f);
            lathed.points.push_back({r * ca, p.y, r * sa});
        }
    }

    const uint32_t rows = static_cast<uint32_t>(profile.size());
    for (int s = 0; s < segments; ++s) {
        const uint32_t row0 = static_cast<uint32_t>(s) * rows;
        const uint32_t row1 = static_cast<uint32_t>((s + 1) % segments) * rows;
        for (uint32_t r = 0; r + 1 < rows; ++r) {
            lathed.faces.push_back(MakeFace({row0 + r, row1 + r, row1 + r + 1, row0 + r + 1}));
        }
    }

    *mesh = std::move(lathed);
    RefreshDocumentView(editor, L"Lathed selected profile around Y.");
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
            editor.loadedPath.clear();
            AddConsoleLine(editor, L"Could not load requested .an8 file. Using built-in cube.");
            for (const std::string& error : result.errors) {
                AddConsoleLine(editor, L"Error: " + ToWide(error));
            }
        }
    } else {
        editor.document = BuildFallbackCubeDocument();
        editor.loadedPath.clear();
        AddConsoleLine(editor, L"No bundled .an8 sample found. Using built-in cube.");
    }

    RebuildMeshViews(editor);
    if (editor.meshes.empty()) {
        AddConsoleLine(editor, L"Loaded document contains no mesh geometry Anim8orX can render yet.");
    }
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
    LoadDocument(editor, path);
    RefreshWindowTitle(editor);
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

std::string EscapeAn8String(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

void WriteIndent(std::ostream& out, int indent) {
    for (int i = 0; i < indent; ++i) {
        out << "  ";
    }
}

void WriteAn8Mesh(std::ostream& out, const An8Mesh& mesh, int indent) {
    WriteIndent(out, indent);
    out << "mesh {\n";
    WriteIndent(out, indent + 1);
    out << '"' << EscapeAn8String(mesh.name.empty() ? "mesh" : mesh.name) << "\"\n";

    WriteIndent(out, indent + 1);
    out << "points {\n";
    out << std::fixed << std::setprecision(5);
    for (const An8Vector3& point : mesh.points) {
        WriteIndent(out, indent + 2);
        out << '(' << point.x << ' ' << point.y << ' ' << point.z << ")\n";
    }
    WriteIndent(out, indent + 1);
    out << "}\n";

    WriteIndent(out, indent + 1);
    out << "faces {\n";
    for (const An8Face& face : mesh.faces) {
        if (face.indices.size() < 3) {
            continue;
        }

        WriteIndent(out, indent + 2);
        out << face.indices.size() << " 4 0 -1 (";
        for (size_t i = 0; i < face.indices.size(); ++i) {
            if (i > 0) {
                out << ' ';
            }
            out << face.indices[i];
        }
        out << ")\n";
    }
    WriteIndent(out, indent + 1);
    out << "}\n";

    WriteIndent(out, indent);
    out << "}\n";
}

void WriteAn8Object(std::ostream& out, const An8Object& object, int indent) {
    WriteIndent(out, indent);
    out << "object {\n";
    WriteIndent(out, indent + 1);
    out << '"' << EscapeAn8String(object.name.empty() ? "object" : object.name) << "\"\n";

    for (const An8Mesh& mesh : object.meshes) {
        WriteAn8Mesh(out, mesh, indent + 1);
    }

    for (const An8Object& child : object.children) {
        WriteAn8Object(out, child, indent + 1);
    }

    WriteIndent(out, indent);
    out << "}\n";
}

bool SaveDocumentToPath(EditorState& editor, const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        AddConsoleLine(editor, L"Could not save .an8 file: " + path.wstring());
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return false;
    }

    out << "header {\n";
    out << "  version { \"1.00\" }\n";
    out << "}\n\n";
    out << "description {\n";
    out << "  \"Saved by Anim8orX\"\n";
    out << "}\n\n";

    for (const An8Object& object : editor.document.objects) {
        WriteAn8Object(out, object, 0);
        out << '\n';
    }

    if (!out) {
        AddConsoleLine(editor, L"Failed while writing .an8 file: " + path.wstring());
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return false;
    }

    editor.loadedPath = path;
    AddConsoleLine(editor, L"Saved .an8 file: " + path.wstring());
    RefreshWindowTitle(editor);
    InvalidateRect(editor.hwnd, nullptr, FALSE);
    return true;
}

std::filesystem::path ShowSaveAn8Dialog(HWND owner) {
    wchar_t fileName[MAX_PATH] = L"Anim8orXScene.an8";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Anim8or Files (*.an8)\0*.an8\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"an8";
    ofn.lpstrTitle = L"Save Anim8orX .an8 File";

    if (!GetSaveFileNameW(&ofn)) {
        return {};
    }

    return std::filesystem::path(fileName);
}

bool SaveDocumentAs(EditorState& editor) {
    const std::filesystem::path path = ShowSaveAn8Dialog(editor.hwnd);
    if (path.empty()) {
        AddConsoleLine(editor, L"Save .an8 cancelled.");
        InvalidateRect(editor.hwnd, nullptr, FALSE);
        return false;
    }

    return SaveDocumentToPath(editor, path);
}

bool SaveDocument(EditorState& editor) {
    if (editor.loadedPath.empty()) {
        return SaveDocumentAs(editor);
    }

    return SaveDocumentToPath(editor, editor.loadedPath);
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

struct CachedBrush {
    COLORREF color = 0;
    HBRUSH handle = nullptr;
};

struct CachedPen {
    COLORREF color = 0;
    int width = 1;
    HPEN handle = nullptr;
};

struct GdiCache {
    std::vector<CachedBrush> brushes;
    std::vector<CachedPen> pens;

    ~GdiCache() {
        for (const CachedBrush& brush : brushes) {
            if (brush.handle != nullptr) {
                DeleteObject(brush.handle);
            }
        }
        for (const CachedPen& pen : pens) {
            if (pen.handle != nullptr) {
                DeleteObject(pen.handle);
            }
        }
    }

    HBRUSH Brush(COLORREF color) {
        for (const CachedBrush& brush : brushes) {
            if (brush.color == color) {
                return brush.handle;
            }
        }

        HBRUSH handle = CreateBrush(color);
        brushes.push_back({color, handle});
        return handle;
    }

    HPEN Pen(COLORREF color, int width) {
        width = std::max(1, width);
        for (const CachedPen& pen : pens) {
            if (pen.color == color && pen.width == width) {
                return pen.handle;
            }
        }

        HPEN handle = CreatePenSolid(color, width);
        pens.push_back({color, width, handle});
        return handle;
    }
};

GdiCache& CachedGdi() {
    static GdiCache cache;
    return cache;
}

void Fill(HDC dc, const RectI& rect, COLORREF color) {
    HBRUSH brush = CachedGdi().Brush(color);
    RECT nativeRect = rect.ToRECT();
    FillRect(dc, &nativeRect, brush);
}

void Stroke(HDC dc, const RectI& rect, COLORREF color) {
    HPEN pen = CachedGdi().Pen(color, 1);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void DrawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
    HPEN pen = CachedGdi().Pen(color, width);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, oldPen);
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
    const int explorer = editor.width >= 1100 ? 244 : editor.width >= 900 ? 200 : 0;
    const int right = editor.width >= 980 ? 344 : 0;
    const int bottom = editor.height >= 680 ? 128 : 92;

    editor.layout.topBar = {0, 0, editor.width, commandStrip};
    editor.layout.toolBar = {0, commandStrip, rail, editor.height - commandStrip - status};
    editor.layout.leftPanel = {rail, commandStrip, explorer, editor.height - commandStrip - bottom - status};
    editor.layout.rightPanel = {editor.width - right, commandStrip, right, editor.height - commandStrip - bottom - status};
    editor.layout.console = {rail, editor.height - bottom - status, editor.width - rail, bottom};
    editor.layout.viewport = {rail + explorer, commandStrip, editor.width - rail - explorer - right, editor.height - commandStrip - bottom - status};
    editor.layout.status = {0, editor.height - status, editor.width, status};
}

ProjectionPoint ProjectPoint(const EditorState& editor, const An8Vector3& point) {
    ProjectionPoint projected;
    const RectI& viewport = editor.layout.viewport;

    const Vec3 world = {point.x, point.y, point.z};
    const Vec3 rel = world - editor.camera.position;
    const float z = anim8orx::Dot(rel, editor.projectionForward);
    if (z <= 0.02f) {
        return projected;
    }

    const float x = anim8orx::Dot(rel, editor.projectionRight);
    const float y = anim8orx::Dot(rel, editor.projectionUp);

    const float ndcX = x / (z * editor.projectionTanHalfFov * editor.projectionAspect);
    const float ndcY = y / (z * editor.projectionTanHalfFov);

    projected.p.x = viewport.x + static_cast<LONG>((ndcX * 0.5f + 0.5f) * static_cast<float>(viewport.w));
    projected.p.y = viewport.y + static_cast<LONG>((0.5f - ndcY * 0.5f) * static_cast<float>(viewport.h));
    projected.visible = projected.p.x > viewport.x - 1000 &&
                        projected.p.x < viewport.x + viewport.w + 1000 &&
                        projected.p.y > viewport.y - 1000 &&
                        projected.p.y < viewport.y + viewport.h + 1000;
    return projected;
}

void UpdateProjectionCache(EditorState& editor) {
    editor.projectionForward = editor.camera.Forward();
    editor.projectionRight = editor.camera.Right();
    editor.projectionUp = editor.camera.Up();
    editor.projectionAspect = editor.layout.viewport.h > 0
        ? static_cast<float>(editor.layout.viewport.w) / static_cast<float>(editor.layout.viewport.h)
        : 1.0f;
    editor.projectionAspect = std::max(editor.projectionAspect, 0.0001f);
    editor.projectionTanHalfFov = std::tan(editor.camera.verticalFovRadians * 0.5f);
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
    HPEN pen = CachedGdi().Pen(color, 1);
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
    const RectI& rail = editor.layout.toolBar;
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
    if (panel.w <= 0 || panel.h <= 0) {
        return;
    }

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
            DrawCheckboxRow(dc, editor, x, y, L"Grid Snap", editor.gridSnap); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Grid Snap Size", L"0.25"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Angle Snap", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Angle Snap Size", L"15.0 deg"); y += 24;
            DrawCheckboxRow(dc, editor, x, y, L"Backface Culling", editor.backfaceCulling); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Auto-Save", true); y += 22;
            DrawInspectorRow(dc, editor, x, y, L"Interval", L"5 min"); y += 30;
            DrawSectionTitle(dc, editor, x, y, L"Viewport Overlays");
            DrawCheckboxRow(dc, editor, x, y, L"Show Grid", editor.showGrid); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Show Axes", editor.showAxes); y += 22;
            DrawCheckboxRow(dc, editor, x, y, L"Show Normals", editor.showNormals); y += 22;
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

    if (editor.showGrid) {
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
    }

    if (editor.showAxes) {
        DrawWorldLine(dc, editor, {-extent, 0.0f, 0.0f}, {extent, 0.0f, 0.0f}, Rgb(190, 73, 73), 2);
        DrawWorldLine(dc, editor, {0.0f, 0.0f, -extent}, {0.0f, 0.0f, extent}, Rgb(76, 152, 88), 2);
        DrawWorldLine(dc, editor, {0.0f, -2.0f, 0.0f}, {0.0f, 8.0f, 0.0f}, Rgb(86, 134, 220), 2);
    }
}

void DrawMeshes(HDC dc, const EditorState& editor) {
    for (size_t meshIndex = 0; meshIndex < editor.meshes.size(); ++meshIndex) {
        const MeshView& mesh = editor.meshes[meshIndex];
        const bool selected = static_cast<int>(meshIndex) == editor.selectedMesh;
        const COLORREF color = selected ? Rgb(239, 189, 87) : Rgb(192, 199, 207);
        const int width = selected ? 2 : 1;
        std::vector<ProjectionPoint> projectedPoints;
        projectedPoints.reserve(mesh.points.size());
        for (const An8Vector3& point : mesh.points) {
            projectedPoints.push_back(ProjectPoint(editor, point));
        }

        for (const An8Face& face : mesh.faces) {
            if (face.indices.size() < 2) {
                continue;
            }

            Vec3 center{};
            int centerCount = 0;
            Vec3 normal{};
            bool hasNormal = false;
            if (face.indices.size() >= 3) {
                const uint32_t ia = face.indices[0];
                const uint32_t ib = face.indices[1];
                const uint32_t ic = face.indices[2];
                if (ia < mesh.points.size() && ib < mesh.points.size() && ic < mesh.points.size()) {
                    const An8Vector3& a = mesh.points[ia];
                    const An8Vector3& b = mesh.points[ib];
                    const An8Vector3& c = mesh.points[ic];
                    normal = anim8orx::Normalize(anim8orx::Cross(
                        Vec3{b.x - a.x, b.y - a.y, b.z - a.z},
                        Vec3{c.x - a.x, c.y - a.y, c.z - a.z}));
                    hasNormal = anim8orx::Length(normal) > 0.0001f;
                }
            }

            if (editor.backfaceCulling || editor.showNormals) {
                for (uint32_t index : face.indices) {
                    if (index < mesh.points.size()) {
                        center += Vec3{mesh.points[index].x, mesh.points[index].y, mesh.points[index].z};
                        ++centerCount;
                    }
                }
                if (centerCount > 0) {
                    center = center / static_cast<float>(centerCount);
                }
            }

            if (editor.backfaceCulling && hasNormal && centerCount > 0) {
                const Vec3 toCamera = anim8orx::Normalize(editor.camera.position - center);
                if (anim8orx::Dot(normal, toCamera) <= 0.0f) {
                    continue;
                }
            }

            for (size_t i = 0; i < face.indices.size(); ++i) {
                const uint32_t a = face.indices[i];
                const uint32_t b = face.indices[(i + 1) % face.indices.size()];
                if (a >= projectedPoints.size() || b >= projectedPoints.size()) {
                    continue;
                }

                const ProjectionPoint& pa = projectedPoints[a];
                const ProjectionPoint& pb = projectedPoints[b];
                if (!pa.visible || !pb.visible) {
                    continue;
                }
                DrawLine(dc, pa.p.x, pa.p.y, pb.p.x, pb.p.y, color, width);
            }

            if (editor.showNormals && face.indices.size() >= 3) {
                if (hasNormal && centerCount > 0) {
                    const Vec3 end = center + normal * 0.45f;
                    DrawWorldLine(dc, editor, {center.x, center.y, center.z}, {end.x, end.y, end.z}, Rgb(92, 174, 188), 1);
                }
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

    Fill(dc, {viewport.x + viewport.w - 396, viewport.y + 8, 380, 24}, Rgb(47, 47, 47));
    Stroke(dc, {viewport.x + viewport.w - 396, viewport.y + 8, 380, 24}, Rgb(88, 88, 88));
    Text(dc, L"RMB+WASD fly  RMB+Wheel speed  Alt+LMB orbit  F focus", viewport.x + viewport.w - 388, viewport.y + 8, 364, 24, Rgb(188, 188, 188), editor.smallFont);
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

void ReleaseBackBuffer(EditorState& editor) {
    if (editor.backBufferDc != nullptr && editor.backBufferOldBitmap != nullptr) {
        SelectObject(editor.backBufferDc, editor.backBufferOldBitmap);
    }
    if (editor.backBufferBitmap != nullptr) {
        DeleteObject(editor.backBufferBitmap);
    }
    if (editor.backBufferDc != nullptr) {
        DeleteDC(editor.backBufferDc);
    }

    editor.backBufferDc = nullptr;
    editor.backBufferBitmap = nullptr;
    editor.backBufferOldBitmap = nullptr;
    editor.backBufferWidth = 0;
    editor.backBufferHeight = 0;
    editor.backBufferValid = false;
}

bool EnsureBackBuffer(EditorState& editor, HDC windowDc) {
    if (editor.backBufferDc != nullptr &&
        editor.backBufferBitmap != nullptr &&
        editor.backBufferWidth == editor.width &&
        editor.backBufferHeight == editor.height) {
        return true;
    }

    ReleaseBackBuffer(editor);

    editor.backBufferDc = CreateCompatibleDC(windowDc);
    if (editor.backBufferDc == nullptr) {
        return false;
    }

    editor.backBufferBitmap = CreateCompatibleBitmap(windowDc, editor.width, editor.height);
    if (editor.backBufferBitmap == nullptr) {
        ReleaseBackBuffer(editor);
        return false;
    }

    editor.backBufferOldBitmap = SelectObject(editor.backBufferDc, editor.backBufferBitmap);
    editor.backBufferWidth = editor.width;
    editor.backBufferHeight = editor.height;
    return editor.backBufferOldBitmap != nullptr;
}

void InvalidateViewport(const EditorState& editor) {
    if (editor.hwnd == nullptr) {
        return;
    }

    RECT viewport = editor.layout.viewport.ToRECT();
    InvalidateRect(editor.hwnd, &viewport, FALSE);
}

bool IsInsideRect(const RECT& inner, const RECT& outer) {
    return inner.left >= outer.left &&
           inner.top >= outer.top &&
           inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}

bool ShouldPaintViewportOnly(const EditorState& editor, const RECT& dirty) {
    if (!editor.backBufferValid || editor.viewMenuOpen) {
        return false;
    }

    const RECT viewport = editor.layout.viewport.ToRECT();
    return IsInsideRect(dirty, viewport);
}

void PaintEditor(HWND hwnd, EditorState& editor) {
    PAINTSTRUCT ps{};
    HDC windowDc = BeginPaint(hwnd, &ps);
    const bool buffered = EnsureBackBuffer(editor, windowDc);
    HDC dc = buffered ? editor.backBufferDc : windowDc;
    const bool viewportOnly = buffered && ShouldPaintViewportOnly(editor, ps.rcPaint);

    if (viewportOnly) {
        UpdateProjectionCache(editor);
        DrawViewport(dc, editor);
    } else {
        Fill(dc, {0, 0, editor.width, editor.height}, Rgb(18, 20, 25));
        DrawTopBar(dc, editor);
        DrawToolBar(dc, editor);
        DrawHierarchy(dc, editor);
        UpdateProjectionCache(editor);
        DrawViewport(dc, editor);
        DrawInspector(dc, editor);
        DrawConsole(dc, editor);
        DrawStatus(dc, editor);
        DrawViewportViewPopup(dc, editor);
        editor.backBufferValid = buffered;
    }

    if (buffered) {
        const int x = ps.rcPaint.left;
        const int y = ps.rcPaint.top;
        const int w = static_cast<int>(std::max<LONG>(0, ps.rcPaint.right - ps.rcPaint.left));
        const int h = static_cast<int>(std::max<LONG>(0, ps.rcPaint.bottom - ps.rcPaint.top));
        BitBlt(windowDc, x, y, w, h, dc, x, y, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

bool IsKeyDown(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool HasPendingViewportInput(const EditorState& editor) {
    return editor.leftMouseDown ||
           editor.middleMouseDown ||
           editor.rightMouseDown ||
           editor.focusPressed ||
           std::abs(editor.accumulatedMouseDx) > 0.0001f ||
           std::abs(editor.accumulatedMouseDy) > 0.0001f ||
           std::abs(editor.accumulatedWheel) > 0.0001f;
}

void StartFrameTimer(HWND hwnd, EditorState& editor) {
    if (editor.frameTimerActive) {
        return;
    }

    SetTimer(hwnd, kFrameTimerId, kFrameMillis, nullptr);
    editor.frameTimerActive = true;
}

void StopFrameTimer(HWND hwnd, EditorState& editor) {
    if (!editor.frameTimerActive || HasPendingViewportInput(editor)) {
        return;
    }

    KillTimer(hwnd, kFrameTimerId);
    editor.frameTimerActive = false;
}

bool TickCamera(EditorState& editor) {
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

    const Vec3 oldPosition = editor.camera.position;
    const Vec3 oldFocus = editor.camera.focus;
    const float oldYaw = editor.camera.yawRadians;
    const float oldPitch = editor.camera.pitchRadians;
    const float oldDistance = editor.camera.orbitDistance;
    const float oldFlySpeed = editor.camera.flySpeed;

    editor.camera.Update(input);
    editor.accumulatedMouseDx = 0.0f;
    editor.accumulatedMouseDy = 0.0f;
    editor.accumulatedWheel = 0.0f;
    editor.focusPressed = false;

    auto changed = [](float a, float b) {
        return std::abs(a - b) > 0.00001f;
    };
    auto changedVec = [&](const Vec3& a, const Vec3& b) {
        return changed(a.x, b.x) || changed(a.y, b.y) || changed(a.z, b.z);
    };

    return changedVec(oldPosition, editor.camera.position) ||
           changedVec(oldFocus, editor.camera.focus) ||
           changed(oldYaw, editor.camera.yawRadians) ||
           changed(oldPitch, editor.camera.pitchRadians) ||
           changed(oldDistance, editor.camera.orbitDistance) ||
           changed(oldFlySpeed, editor.camera.flySpeed);
}

void CaptureViewportMouse(HWND hwnd, EditorState& editor, int x, int y) {
    editor.viewportActive = true;
    editor.lastMouse = POINT{x, y};
    SetCapture(hwnd);
    SetFocus(hwnd);
    StartFrameTimer(hwnd, editor);
    InvalidateViewport(editor);
}

void ReleaseViewportMouse(EditorState& editor) {
    if (!editor.leftMouseDown && !editor.middleMouseDown && !editor.rightMouseDown) {
        ReleaseCapture();
        StopFrameTimer(editor.hwnd, editor);
        InvalidateViewport(editor);
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
    const RectI& rail = editor.layout.toolBar;
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
        {L"Object", {L"New Mesh", L"Convert to Mesh", L"Join Solids", L"Subdivide Faces", L"Extrude", L"Inset", L"Lathe", L"Mirror", L"Smooth", nullptr}},
        {L"Options", {L"Grid", L"Snapping", L"Show Axis", L"Show Normals", L"Backface Culling", L"Theme", nullptr}},
        {L"View", {L"All", L"Front", L"Back", L"Left", L"Right", L"Top", L"Bottom", L"Ortho", L"Perspective", L"Frame Selection", nullptr}},
        {L"Build", {L"Add Cube", L"Add Sphere", L"Add Cylinder", L"Add Cone", L"Add Torus", L"Add Text", L"Add Bone", L"Add Camera", L"Add Light", nullptr}},
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
                } else if (i == 2) {
                    id = kMenuFileSave;
                } else if (i == 3) {
                    id = kMenuFileSaveAs;
                } else if (i == 4) {
                    id = kMenuFileImport;
                } else if (i == 5) {
                    id = kMenuFileExport;
                } else if (i == 6) {
                    id = kMenuOptionPreferences;
                } else if (i == 7) {
                    id = kMenuFileExit;
                }
            } else if (wcscmp(spec.name, L"Edit") == 0) {
                if (i == 5) {
                    id = kMenuEditDelete;
                }
            } else if (wcscmp(spec.name, L"Mode") == 0) {
                id = i == 0 ? kMenuModeObject : i == 1 ? kMenuModeFigure : i == 2 ? kMenuModeSequence : kMenuModeScene;
            } else if (wcscmp(spec.name, L"Object") == 0) {
                if (i == 3) {
                    id = kMenuObjectSubdivide;
                } else if (i == 4) {
                    id = kMenuObjectExtrude;
                } else if (i == 5) {
                    id = kMenuObjectInset;
                } else if (i == 6) {
                    id = kMenuObjectLathe;
                } else if (i == 7) {
                    id = kMenuObjectMirror;
                }
            } else if (wcscmp(spec.name, L"Options") == 0) {
                if (i == 0) {
                    id = kMenuOptionGrid;
                } else if (i == 1) {
                    id = kMenuOptionSnapping;
                } else if (i == 2) {
                    id = kMenuOptionAxis;
                } else if (i == 3) {
                    id = kMenuOptionNormals;
                } else if (i == 4) {
                    id = kMenuOptionBackface;
                } else if (i == 5) {
                    id = kMenuOptionPreferences;
                }
            } else if (wcscmp(spec.name, L"View") == 0) {
                id = kMenuViewAll + i;
            } else if (wcscmp(spec.name, L"Build") == 0) {
                if (i == 0) {
                    id = kMenuBuildCube;
                } else if (i == 1) {
                    id = kMenuBuildSphere;
                } else if (i == 2) {
                    id = kMenuBuildCylinder;
                } else if (i == 3) {
                    id = kMenuBuildCone;
                } else if (i == 4) {
                    id = kMenuBuildTorus;
                } else if (i == 5) {
                    id = kMenuBuildText;
                } else if (i == 6) {
                    id = kMenuBuildBone;
                } else if (i == 7) {
                    id = kMenuBuildCamera;
                } else if (i == 8) {
                    id = kMenuBuildLight;
                }
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
            return 0;

        case WM_SIZE:
            editor.width = std::max(640, static_cast<int>(LOWORD(lParam)));
            editor.height = std::max(480, static_cast<int>(HIWORD(lParam)));
            CalculateLayout(editor);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            if (wParam == kFrameTimerId) {
                if (TickCamera(editor)) {
                    InvalidateViewport(editor);
                }
                StopFrameTimer(hwnd, editor);
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
                case kMenuFileImport:
                    OpenAn8FromDialog(editor);
                    break;
                case kMenuFileSave:
                    SaveDocument(editor);
                    break;
                case kMenuFileSaveAs:
                    SaveDocumentAs(editor);
                    break;
                case kMenuFileExport:
                    SaveDocumentAs(editor);
                    break;
                case kMenuFileExit:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    break;
                case kMenuEditDelete:
                    DeleteSelectedMesh(editor);
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
                case kMenuViewFrameSelection:
                    editor.camera.FocusOn(editor.selectionCenter, editor.selectionRadius);
                    AddConsoleLine(editor, L"Framed selected object.");
                    break;
                case kMenuOptionGrid:
                    editor.showGrid = !editor.showGrid;
                    editor.propertyPage = 0;
                    AddConsoleLine(editor, editor.showGrid ? L"Grid enabled." : L"Grid disabled.");
                    break;
                case kMenuOptionSnapping:
                    editor.gridSnap = !editor.gridSnap;
                    editor.propertyPage = 0;
                    AddConsoleLine(editor, editor.gridSnap ? L"Grid snapping enabled." : L"Grid snapping disabled.");
                    break;
                case kMenuOptionAxis:
                    editor.showAxes = !editor.showAxes;
                    editor.propertyPage = 0;
                    AddConsoleLine(editor, editor.showAxes ? L"Axes enabled." : L"Axes disabled.");
                    break;
                case kMenuOptionNormals:
                    editor.showNormals = !editor.showNormals;
                    editor.propertyPage = 0;
                    AddConsoleLine(editor, editor.showNormals ? L"Normals enabled." : L"Normals disabled.");
                    break;
                case kMenuOptionBackface:
                    editor.backfaceCulling = !editor.backfaceCulling;
                    editor.propertyPage = 0;
                    AddConsoleLine(editor, editor.backfaceCulling ? L"Backface culling enabled." : L"Backface culling disabled.");
                    break;
                case kMenuOptionPreferences:
                    editor.propertyPage = 0;
                    AddConsoleLine(editor, L"Opened preferences.");
                    break;
                case kMenuObjectSubdivide:
                    SubdivideSelectedMesh(editor);
                    break;
                case kMenuObjectExtrude:
                    ExtrudeSelectedMesh(editor);
                    break;
                case kMenuObjectInset:
                    InsetSelectedMesh(editor);
                    break;
                case kMenuObjectLathe:
                    LatheSelectedMesh(editor);
                    break;
                case kMenuObjectMirror:
                    MirrorSelectedMesh(editor);
                    break;
                case kMenuBuildCube:
                    SetEditorMode(editor, 0);
                    AddMeshObject(editor, "Cube", MakeCubeMesh("Cube_Mesh", 2.0f));
                    break;
                case kMenuBuildSphere:
                    SetEditorMode(editor, 0);
                    AddMeshObject(editor, "Sphere", MakeSphereMesh("Sphere_Mesh", 1.0f, 12, 24));
                    break;
                case kMenuBuildCylinder:
                    SetEditorMode(editor, 0);
                    AddMeshObject(editor, "Cylinder", MakeCylinderMesh("Cylinder_Mesh", 1.0f, 1.0f, 2.0f, 24));
                    break;
                case kMenuBuildCone:
                    SetEditorMode(editor, 0);
                    AddMeshObject(editor, "Cone", MakeCylinderMesh("Cone_Mesh", 0.0f, 1.0f, 2.0f, 24));
                    break;
                case kMenuBuildTorus:
                    SetEditorMode(editor, 0);
                    AddMeshObject(editor, "Torus", MakeTorusMesh("Torus_Mesh", 1.1f, 0.28f, 24, 16));
                    break;
                case kMenuBuildText:
                    SetEditorMode(editor, 0);
                    AddMeshObject(editor, "Text", MakeTextHelperMesh());
                    break;
                case kMenuBuildBone:
                    SetEditorMode(editor, 1);
                    AddMeshObject(editor, "Bone", MakeBoneHelperMesh());
                    break;
                case kMenuBuildCamera:
                    SetEditorMode(editor, 3);
                    AddMeshObject(editor, "Camera", MakeCameraHelperMesh());
                    break;
                case kMenuBuildLight:
                    SetEditorMode(editor, 3);
                    AddMeshObject(editor, "Light", MakeLightHelperMesh());
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
                ClickHierarchy(editor, x, y);
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
            const bool wasViewportActive = editor.viewportActive;
            if (editor.leftMouseDown || editor.middleMouseDown || editor.rightMouseDown) {
                editor.accumulatedMouseDx += static_cast<float>(x - editor.lastMouse.x);
                editor.accumulatedMouseDy += static_cast<float>(y - editor.lastMouse.y);
                StartFrameTimer(hwnd, editor);
            }
            editor.viewportActive = editor.layout.viewport.Contains(x, y) || editor.leftMouseDown || editor.middleMouseDown || editor.rightMouseDown;
            editor.lastMouse = POINT{x, y};
            if (wasViewportActive != editor.viewportActive) {
                InvalidateViewport(editor);
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &screenPoint);
            if (editor.layout.viewport.Contains(screenPoint.x, screenPoint.y)) {
                editor.viewportActive = true;
                editor.accumulatedWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
                StartFrameTimer(hwnd, editor);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (IsKeyDown(VK_CONTROL) && wParam == 'N') {
                NewDefaultDocument(editor);
            } else if (IsKeyDown(VK_CONTROL) && wParam == 'O') {
                OpenAn8FromDialog(editor);
            } else if (IsKeyDown(VK_CONTROL) && wParam == 'S') {
                SaveDocument(editor);
            } else if (wParam == VK_DELETE) {
                DeleteSelectedMesh(editor);
            } else if (wParam == 'F') {
                editor.focusPressed = true;
                StartFrameTimer(hwnd, editor);
            } else if (wParam == VK_ESCAPE) {
                editor.viewportActive = false;
                editor.leftMouseDown = false;
                editor.middleMouseDown = false;
                editor.rightMouseDown = false;
                editor.accumulatedMouseDx = 0.0f;
                editor.accumulatedMouseDy = 0.0f;
                editor.accumulatedWheel = 0.0f;
                editor.focusPressed = false;
                ReleaseCapture();
                StopFrameTimer(hwnd, editor);
                InvalidateViewport(editor);
            }
            return 0;

        case WM_DESTROY:
            DragAcceptFiles(hwnd, FALSE);
            if (editor.frameTimerActive) {
                KillTimer(hwnd, kFrameTimerId);
                editor.frameTimerActive = false;
            }
            ReleaseBackBuffer(editor);
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
