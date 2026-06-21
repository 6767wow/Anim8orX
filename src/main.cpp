#include <Anim8orX/Import/An8Parser.hpp>
#include <Anim8orX/Viewport/Camera.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path FindBundledSample(char** argv) {
    const std::filesystem::path executablePath =
        std::filesystem::absolute(std::filesystem::path(argv[0])).parent_path();

    const std::vector<std::filesystem::path> candidates = {
        executablePath / "examples" / "cube.an8",
        executablePath.parent_path() / "examples" / "cube.an8",
        std::filesystem::current_path() / "examples" / "cube.an8"
    };

    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

void WaitBeforeExitIfNeeded(bool shouldWait) {
    if (!shouldWait) {
        return;
    }

    std::cout << "\nPress Enter to exit...";
    std::cout.flush();
    std::string ignored;
    std::getline(std::cin, ignored);
}

} // namespace

int main(int argc, char** argv) {
    const bool launchedWithoutFile = argc < 2;
    std::string path;

    if (launchedWithoutFile) {
        const std::filesystem::path samplePath = FindBundledSample(argv);
        if (samplePath.empty()) {
            std::cerr << "usage: Anim8orX <file.an8>\n";
            std::cerr << "No file was provided and examples/cube.an8 was not found.\n";
            WaitBeforeExitIfNeeded(true);
            return 2;
        }

        std::cout << "No .an8 file argument was provided. Loading bundled sample:\n"
                  << samplePath.string() << "\n\n";
        path = samplePath.string();
    } else {
        path = argv[1];
    }

    const anim8orx::An8LoadResult result = anim8orx::LoadAn8File(path);

    for (const std::string& warning : result.warnings) {
        std::cerr << "warning: " << warning << "\n";
    }

    if (!result.ok) {
        for (const std::string& error : result.errors) {
            std::cerr << "error: " << error << "\n";
        }
        WaitBeforeExitIfNeeded(launchedWithoutFile);
        return 1;
    }

    std::cout << "Loaded: " << path << "\n";
    std::cout << "Objects: " << result.document.objects.size() << "\n";

    size_t meshCount = 0;
    size_t pointCount = 0;
    size_t faceCount = 0;
    size_t triangleIndexCount = 0;

    for (const anim8orx::An8Object& object : result.document.objects) {
        std::cout << "object \"" << object.name << "\" meshes=" << object.meshes.size() << "\n";
        meshCount += object.meshes.size();

        for (const anim8orx::An8Mesh& mesh : object.meshes) {
            std::vector<std::string> geometryWarnings;
            const anim8orx::An8Geometry geometry =
                anim8orx::BuildTriangleGeometry(mesh, &geometryWarnings);

            pointCount += mesh.points.size();
            faceCount += mesh.faces.size();
            triangleIndexCount += geometry.indices.size();

            std::cout << "  mesh \"" << mesh.name << "\""
                      << " points=" << mesh.points.size()
                      << " faces=" << mesh.faces.size()
                      << " triangles=" << geometry.indices.size() / 3
                      << "\n";

            for (const std::string& warning : geometryWarnings) {
                std::cerr << "warning: " << warning << "\n";
            }
        }
    }

    anim8orx::ViewportCamera camera;
    const anim8orx::Mat4 view = camera.ViewMatrix();
    const anim8orx::Mat4 projection = camera.ProjectionMatrix(16.0f / 9.0f);

    std::cout << "Summary: meshes=" << meshCount
              << " points=" << pointCount
              << " faces=" << faceCount
              << " triangle_indices=" << triangleIndexCount
              << "\n";
    std::cout << "Camera ready: view[0]=" << view.m[0]
              << " projection[0]=" << projection.m[0]
              << "\n";

    WaitBeforeExitIfNeeded(launchedWithoutFile);
    return 0;
}
