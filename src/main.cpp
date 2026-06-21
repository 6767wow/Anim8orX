#include <Anim8orX/Import/An8Parser.hpp>
#include <Anim8orX/Viewport/Camera.hpp>

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: anim8orx_sandbox <file.an8>\n";
        return 2;
    }

    const std::string path = argv[1];
    const anim8orx::An8LoadResult result = anim8orx::LoadAn8File(path);

    for (const std::string& warning : result.warnings) {
        std::cerr << "warning: " << warning << "\n";
    }

    if (!result.ok) {
        for (const std::string& error : result.errors) {
            std::cerr << "error: " << error << "\n";
        }
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

    return 0;
}

