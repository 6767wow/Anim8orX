#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace anim8orx {

constexpr float AN8_PI = 3.14159265358979323846f;

struct An8Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct An8Vector2 {
    float u = 0.0f;
    float v = 0.0f;
};

struct An8Color {
    int r = 160;
    int g = 170;
    int b = 180;
    bool valid = false;
};

struct An8Texture {
    std::string name;
    std::string filePath;
};

struct An8Material {
    std::string name;
    An8Color diffuse;
    std::string textureName;
    std::string textureKind;
};

struct An8Face {
    std::vector<uint32_t> indices;
    std::vector<uint32_t> texcoordIndices;
    int materialIndex = -1;
};

struct An8Mesh {
    std::string name;
    std::string defaultMaterialName;
    std::vector<std::string> materialNames;
    std::vector<An8Vector3> points;
    std::vector<An8Vector2> texcoords;
    std::vector<An8Face> faces;
};

struct An8Object {
    std::string name;
    std::vector<An8Material> materials;
    std::vector<An8Mesh> meshes;
    std::vector<An8Object> children;
};

struct An8Document {
    std::vector<An8Texture> textures;
    std::vector<An8Object> objects;
};

struct An8Vertex {
    An8Vector3 position;
};

struct An8Geometry {
    std::vector<An8Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct An8LoadResult {
    bool ok = false;
    An8Document document;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

enum class An8TokenKind {
    End,
    Identifier,
    String,
    Number,
    LBrace,
    RBrace,
    LParen,
    RParen,
    Unknown
};

struct An8Token {
    An8TokenKind kind = An8TokenKind::End;
    std::string text;
    double number = 0.0;
    int line = 1;
    int column = 1;
};

class An8Lexer {
public:
    explicit An8Lexer(std::string source)
        : source_(std::move(source)) {}

    An8Token Next() {
        SkipWhitespaceAndComments();

        An8Token t;
        t.line = line_;
        t.column = column_;

        if (AtEnd()) {
            t.kind = An8TokenKind::End;
            return t;
        }

        const char c = Peek();
        switch (c) {
            case '{': Advance(); t.kind = An8TokenKind::LBrace; t.text = "{"; return t;
            case '}': Advance(); t.kind = An8TokenKind::RBrace; t.text = "}"; return t;
            case '(': Advance(); t.kind = An8TokenKind::LParen; t.text = "("; return t;
            case ')': Advance(); t.kind = An8TokenKind::RParen; t.text = ")"; return t;
            case '"': return ReadString();
            default: break;
        }

        if (IsNumberStart(c)) {
            return ReadNumber();
        }

        if (IsIdentifierStart(c)) {
            return ReadIdentifier();
        }

        t.kind = An8TokenKind::Unknown;
        t.text.push_back(c);
        Advance();
        return t;
    }

private:
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;

    bool AtEnd() const { return pos_ >= source_.size(); }

    char Peek(size_t offset = 0) const {
        const size_t p = pos_ + offset;
        return p < source_.size() ? source_[p] : '\0';
    }

    char Advance() {
        const char c = source_[pos_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    void SkipWhitespaceAndComments() {
        for (;;) {
            while (!AtEnd() && std::isspace(static_cast<unsigned char>(Peek()))) {
                Advance();
            }

            if (Peek() == '/' && Peek(1) == '/') {
                while (!AtEnd() && Peek() != '\n') {
                    Advance();
                }
                continue;
            }

            if (Peek() == '/' && Peek(1) == '*') {
                Advance();
                Advance();
                while (!AtEnd()) {
                    if (Peek() == '*' && Peek(1) == '/') {
                        Advance();
                        Advance();
                        break;
                    }
                    Advance();
                }
                continue;
            }

            break;
        }
    }

    bool IsIdentifierStart(char c) const {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    }

    bool IsIdentifierChar(char c) const {
        return !std::isspace(static_cast<unsigned char>(c)) &&
               c != '{' && c != '}' &&
               c != '(' && c != ')' &&
               c != '"';
    }

    bool IsNumberStart(char c) const {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            return true;
        }
        if (c == '.' && std::isdigit(static_cast<unsigned char>(Peek(1)))) {
            return true;
        }
        if ((c == '-' || c == '+') &&
            (std::isdigit(static_cast<unsigned char>(Peek(1))) || Peek(1) == '.')) {
            return true;
        }
        return false;
    }

    An8Token ReadIdentifier() {
        An8Token t;
        t.kind = An8TokenKind::Identifier;
        t.line = line_;
        t.column = column_;
        while (!AtEnd() && IsIdentifierChar(Peek())) {
            t.text.push_back(Advance());
        }
        return t;
    }

    An8Token ReadString() {
        An8Token t;
        t.kind = An8TokenKind::String;
        t.line = line_;
        t.column = column_;

        Advance(); // Opening quote.
        while (!AtEnd() && Peek() != '"') {
            char c = Advance();
            if (c == '\\' && !AtEnd()) {
                const char e = Advance();
                switch (e) {
                    case 'n': t.text.push_back('\n'); break;
                    case 't': t.text.push_back('\t'); break;
                    case '"': t.text.push_back('"'); break;
                    case '\\': t.text.push_back('\\'); break;
                    default: t.text.push_back(e); break;
                }
            } else {
                t.text.push_back(c);
            }
        }

        if (!AtEnd() && Peek() == '"') {
            Advance();
        }
        return t;
    }

    An8Token ReadNumber() {
        An8Token t;
        t.kind = An8TokenKind::Number;
        t.line = line_;
        t.column = column_;

        if (Peek() == '-' || Peek() == '+') {
            t.text.push_back(Advance());
        }
        while (std::isdigit(static_cast<unsigned char>(Peek()))) {
            t.text.push_back(Advance());
        }
        if (Peek() == '.') {
            t.text.push_back(Advance());
            while (std::isdigit(static_cast<unsigned char>(Peek()))) {
                t.text.push_back(Advance());
            }
        }
        if (Peek() == 'e' || Peek() == 'E') {
            t.text.push_back(Advance());
            if (Peek() == '-' || Peek() == '+') {
                t.text.push_back(Advance());
            }
            while (std::isdigit(static_cast<unsigned char>(Peek()))) {
                t.text.push_back(Advance());
            }
        }

        char* end = nullptr;
        t.number = std::strtod(t.text.c_str(), &end);
        return t;
    }
};

class An8Parser {
public:
    explicit An8Parser(std::string source)
        : lexer_(std::move(source)) {
        Advance();
    }

    An8Document ParseDocument() {
        An8Document doc;

        while (!Check(An8TokenKind::End)) {
            if (CheckIdentifier("object")) {
                Advance();
                An8Object object = ParseObject();
                if (object.name.empty()) {
                    object.name = "Object_" + std::to_string(doc.objects.size());
                }
                doc.objects.push_back(std::move(object));
            } else if (CheckIdentifier("texture")) {
                Advance();
                An8Texture texture = ParseTexture();
                if (!texture.name.empty()) {
                    doc.textures.push_back(std::move(texture));
                }
            } else {
                SkipTopLevelEntry();
            }
        }

        return doc;
    }

    const std::vector<std::string>& Errors() const { return errors_; }
    const std::vector<std::string>& Warnings() const { return warnings_; }

private:
    An8Lexer lexer_;
    An8Token current_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;

    void Advance() {
        current_ = lexer_.Next();
    }

    bool Check(An8TokenKind kind) const {
        return current_.kind == kind;
    }

    bool CheckIdentifier(const char* text) const {
        return current_.kind == An8TokenKind::Identifier && current_.text == text;
    }

    bool Match(An8TokenKind kind) {
        if (!Check(kind)) {
            return false;
        }
        Advance();
        return true;
    }

    bool Expect(An8TokenKind kind, const char* message) {
        if (Match(kind)) {
            return true;
        }
        Error(message);
        return false;
    }

    void Error(const std::string& message) {
        std::ostringstream out;
        out << "line " << current_.line << ", column " << current_.column << ": " << message;
        errors_.push_back(out.str());
    }

    void Warning(const std::string& message) {
        std::ostringstream out;
        out << "line " << current_.line << ", column " << current_.column << ": " << message;
        warnings_.push_back(out.str());
    }

    An8Object ParseObject() {
        An8Object object;

        if (!Expect(An8TokenKind::LBrace, "expected '{' after object")) {
            return object;
        }

        if (Check(An8TokenKind::String)) {
            object.name = current_.text;
            Advance();
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("mesh") || CheckIdentifier("subdivision")) {
                const bool subdivision = CheckIdentifier("subdivision");
                Advance();
                An8Mesh mesh = ParseMesh();
                if (mesh.name.empty()) {
                    mesh.name = object.name.empty()
                        ? "Mesh_" + std::to_string(object.meshes.size())
                        : object.name + "_Mesh_" + std::to_string(object.meshes.size());
                }
                if (subdivision && mesh.name.find("subdivision") == std::string::npos) {
                    mesh.name += " subdivision";
                }
                object.meshes.push_back(std::move(mesh));
            } else if (IsPrimitiveComponent()) {
                const std::string kind = current_.text;
                Advance();
                An8Mesh mesh = ParsePrimitiveComponent(kind);
                if (mesh.name.empty()) {
                    mesh.name = kind + "_" + std::to_string(object.meshes.size());
                }
                object.meshes.push_back(std::move(mesh));
            } else if (CheckIdentifier("object")) {
                Advance();
                An8Object child = ParseObject();
                if (child.name.empty()) {
                    child.name = "Child_" + std::to_string(object.children.size());
                }
                object.children.push_back(std::move(child));
            } else if (CheckIdentifier("group")) {
                Advance();
                An8Object group = ParseGroup();
                if (group.name.empty()) {
                    group.name = "Group_" + std::to_string(object.children.size());
                }
                object.children.push_back(std::move(group));
            } else if (CheckIdentifier("material")) {
                Advance();
                An8Material material = ParseMaterialDefinition();
                if (!material.name.empty()) {
                    object.materials.push_back(std::move(material));
                }
            } else if (CheckIdentifier("name")) {
                ParseNameProperty(object.name);
            } else if (Check(An8TokenKind::String) && object.name.empty()) {
                object.name = current_.text;
                Advance();
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close object");
        return object;
    }

    An8Object ParseGroup() {
        An8Object group;

        if (Check(An8TokenKind::String)) {
            group.name = current_.text;
            Advance();
        }

        if (!Expect(An8TokenKind::LBrace, "expected '{' after group")) {
            return group;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("mesh") || CheckIdentifier("subdivision")) {
                const bool subdivision = CheckIdentifier("subdivision");
                Advance();
                An8Mesh mesh = ParseMesh();
                if (mesh.name.empty()) {
                    mesh.name = group.name.empty()
                        ? "Mesh_" + std::to_string(group.meshes.size())
                        : group.name + "_Mesh_" + std::to_string(group.meshes.size());
                }
                if (subdivision && mesh.name.find("subdivision") == std::string::npos) {
                    mesh.name += " subdivision";
                }
                group.meshes.push_back(std::move(mesh));
            } else if (IsPrimitiveComponent()) {
                const std::string kind = current_.text;
                Advance();
                An8Mesh mesh = ParsePrimitiveComponent(kind);
                if (mesh.name.empty()) {
                    mesh.name = kind + "_" + std::to_string(group.meshes.size());
                }
                group.meshes.push_back(std::move(mesh));
            } else if (CheckIdentifier("group")) {
                Advance();
                An8Object child = ParseGroup();
                if (child.name.empty()) {
                    child.name = "Group_" + std::to_string(group.children.size());
                }
                group.children.push_back(std::move(child));
            } else if (CheckIdentifier("material")) {
                Advance();
                An8Material material = ParseMaterialDefinition();
                if (!material.name.empty()) {
                    group.materials.push_back(std::move(material));
                }
            } else if (CheckIdentifier("name")) {
                ParseNameProperty(group.name);
            } else if (Check(An8TokenKind::String) && group.name.empty()) {
                group.name = current_.text;
                Advance();
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close group");
        return group;
    }

    struct An8Quaternion {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct An8BaseTransform {
        An8Vector3 origin;
        An8Quaternion orientation;
        bool hasOrigin = false;
        bool hasOrientation = false;
    };

    An8Mesh ParseMesh() {
        An8Mesh mesh;
        An8BaseTransform baseTransform;

        if (Check(An8TokenKind::String)) {
            mesh.name = current_.text;
            Advance();
        }

        if (!Expect(An8TokenKind::LBrace, "expected '{' after mesh")) {
            return mesh;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (Check(An8TokenKind::String) && mesh.name.empty()) {
                mesh.name = current_.text;
                Advance();
            } else if (CheckIdentifier("name")) {
                ParseNameProperty(mesh.name);
            } else if (CheckIdentifier("points")) {
                Advance();
                mesh.points = ParsePoints();
            } else if (CheckIdentifier("texcoords")) {
                Advance();
                mesh.texcoords = ParseTexcoords();
            } else if (CheckIdentifier("faces")) {
                Advance();
                mesh.faces = ParseFaces();
            } else if (CheckIdentifier("base")) {
                Advance();
                ParseBaseTransform(baseTransform);
            } else if (CheckIdentifier("material")) {
                Advance();
                ParseMeshMaterialReference(mesh.defaultMaterialName);
            } else if (CheckIdentifier("materiallist")) {
                Advance();
                mesh.materialNames = ParseMaterialList();
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close mesh");
        ApplyBaseTransform(mesh, baseTransform);
        return mesh;
    }

    bool IsPrimitiveComponent() const {
        return CheckIdentifier("cube") ||
               CheckIdentifier("sphere") ||
               CheckIdentifier("cylinder");
    }

    An8Mesh ParsePrimitiveComponent(const std::string& kind) {
        std::string name = kind;

        float cubeX = 2.0f;
        float cubeY = 2.0f;
        float cubeZ = 2.0f;

        float sphereRadius = 1.0f;
        int sphereRings = 12;
        int sphereSegments = 24;

        float cylinderLength = 2.0f;
        float cylinderBottomRadius = 1.0f;
        float cylinderTopRadius = 1.0f;
        bool hasTopDiameter = false;
        bool capStart = false;
        bool capEnd = false;
        int cylinderSegments = 24;
        An8BaseTransform baseTransform;

        if (!Expect(An8TokenKind::LBrace, "expected '{' after primitive component")) {
            return {};
        }

        if (Check(An8TokenKind::String)) {
            name = current_.text;
            Advance();
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (Check(An8TokenKind::String) && name == kind) {
                name = current_.text;
                Advance();
            } else if (CheckIdentifier("name")) {
                ParseNameProperty(name);
            } else if (CheckIdentifier("base")) {
                Advance();
                ParseBaseTransform(baseTransform);
            } else if (kind == "cube" && CheckIdentifier("size")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(3, "expected '{' after size");
                if (!values.empty()) {
                    cubeX = static_cast<float>(values[0]);
                    cubeY = values.size() > 1 ? static_cast<float>(values[1]) : cubeX;
                    cubeZ = values.size() > 2 ? static_cast<float>(values[2]) : cubeX;
                }
            } else if (kind == "sphere" && CheckIdentifier("diameter")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(1, "expected '{' after diameter");
                if (!values.empty()) {
                    sphereRadius = std::max(0.01f, static_cast<float>(values[0]) * 0.5f);
                }
            } else if (kind == "sphere" && CheckIdentifier("radius")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(1, "expected '{' after radius");
                if (!values.empty()) {
                    sphereRadius = std::max(0.01f, static_cast<float>(values[0]));
                }
            } else if (kind == "sphere" && CheckIdentifier("longlat")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(2, "expected '{' after longlat");
                if (values.size() >= 2) {
                    sphereRings = std::max(3, static_cast<int>(values[0]));
                    sphereSegments = std::max(6, static_cast<int>(values[1]));
                }
            } else if (kind == "sphere" && CheckIdentifier("geodesic")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(1, "expected '{' after geodesic");
                if (!values.empty()) {
                    const int detail = std::max(1, static_cast<int>(values[0]));
                    sphereRings = std::clamp(detail * 4, 4, 48);
                    sphereSegments = std::clamp(detail * 8, 8, 96);
                }
            } else if (kind == "cylinder" && CheckIdentifier("length")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(1, "expected '{' after length");
                if (!values.empty()) {
                    cylinderLength = std::max(0.01f, static_cast<float>(values[0]));
                }
            } else if (kind == "cylinder" && CheckIdentifier("diameter")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(1, "expected '{' after diameter");
                if (!values.empty()) {
                    cylinderBottomRadius = std::max(0.0f, static_cast<float>(values[0]) * 0.5f);
                    if (!hasTopDiameter) {
                        cylinderTopRadius = cylinderBottomRadius;
                    }
                }
            } else if (kind == "cylinder" && CheckIdentifier("topdiameter")) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(1, "expected '{' after topdiameter");
                if (!values.empty()) {
                    cylinderTopRadius = std::max(0.0f, static_cast<float>(values[0]) * 0.5f);
                    hasTopDiameter = true;
                }
            } else if (kind == "cylinder" && (CheckIdentifier("segments") || CheckIdentifier("longlat") || CheckIdentifier("divisions"))) {
                Advance();
                const std::vector<double> values = ParseNumberChunk(2, "expected '{' after segment count");
                if (!values.empty()) {
                    cylinderSegments = std::max(3, static_cast<int>(values.back()));
                }
            } else if (kind == "cylinder" && CheckIdentifier("capstart")) {
                Advance();
                capStart = true;
                if (Check(An8TokenKind::LBrace)) {
                    SkipBalancedBlock();
                }
            } else if (kind == "cylinder" && CheckIdentifier("capend")) {
                Advance();
                capEnd = true;
                if (Check(An8TokenKind::LBrace)) {
                    SkipBalancedBlock();
                }
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close primitive component");

        An8Mesh mesh;
        if (kind == "cube") {
            mesh = BuildCubePrimitive(name, cubeX, cubeY, cubeZ);
        } else if (kind == "sphere") {
            mesh = BuildSpherePrimitive(name, sphereRadius, sphereRings, sphereSegments);
        } else if (kind == "cylinder") {
            mesh = BuildCylinderPrimitive(name, cylinderTopRadius, cylinderBottomRadius, cylinderLength, cylinderSegments, capStart, capEnd);
        }

        ApplyBaseTransform(mesh, baseTransform);
        return mesh;
    }

    An8Texture ParseTexture() {
        An8Texture texture;
        if (!Expect(An8TokenKind::LBrace, "expected '{' after texture")) {
            return texture;
        }

        if (Check(An8TokenKind::String)) {
            texture.name = current_.text;
            Advance();
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("name")) {
                Advance();
                ParseStringChunk(texture.name, "expected '{' after texture name");
            } else if (CheckIdentifier("file")) {
                Advance();
                ParseStringChunk(texture.filePath, "expected '{' after texture file");
            } else if (Check(An8TokenKind::String) && texture.name.empty()) {
                texture.name = current_.text;
                Advance();
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close texture");
        return texture;
    }

    An8Material ParseMaterialDefinition() {
        An8Material material;
        if (!Expect(An8TokenKind::LBrace, "expected '{' after material")) {
            return material;
        }

        if (Check(An8TokenKind::String)) {
            material.name = current_.text;
            Advance();
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("name")) {
                Advance();
                ParseStringChunk(material.name, "expected '{' after material name");
            } else if (CheckIdentifier("surface")) {
                Advance();
                ParseSurface(material);
            } else if (Check(An8TokenKind::String) && material.name.empty()) {
                material.name = current_.text;
                Advance();
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close material");
        return material;
    }

    void ParseSurface(An8Material& material) {
        if (!Expect(An8TokenKind::LBrace, "expected '{' after surface")) {
            return;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("rgb")) {
                Advance();
                ParseRgbChunk(material.diffuse);
            } else if (CheckIdentifier("diffuse")) {
                Advance();
                ParseDiffuseBlock(material);
            } else if (CheckIdentifier("map")) {
                Advance();
                ParseMaterialMap(material);
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close surface");
    }

    void ParseDiffuseBlock(An8Material& material) {
        if (!Expect(An8TokenKind::LBrace, "expected '{' after diffuse")) {
            return;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("rgb")) {
                Advance();
                ParseRgbChunk(material.diffuse);
            } else if (CheckIdentifier("texturename") || CheckIdentifier("texture")) {
                Advance();
                ParseStringChunk(material.textureName, "expected '{' after diffuse texture name");
                material.textureKind = "diffuse";
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' after diffuse");
    }

    void ParseMaterialMap(An8Material& material) {
        std::string kind;
        std::string textureName;

        if (!Expect(An8TokenKind::LBrace, "expected '{' after map")) {
            return;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("kind")) {
                Advance();
                ParseStringChunk(kind, "expected '{' after map kind");
            } else if (CheckIdentifier("texturename") || CheckIdentifier("texture")) {
                Advance();
                ParseStringChunk(textureName, "expected '{' after texture name");
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' after map");
        if (!textureName.empty()) {
            const std::string loweredKind = ToLowerAscii(kind);
            if (material.textureName.empty() ||
                (loweredKind != "bumpmap" && loweredKind != "normalmap")) {
                material.textureName = textureName;
                material.textureKind = loweredKind;
            }
        }
    }

    void ParseMeshMaterialReference(std::string& outName) {
        ParseStringChunk(outName, "expected '{' after mesh material");
    }

    std::vector<std::string> ParseMaterialList() {
        std::vector<std::string> materialNames;
        if (!Expect(An8TokenKind::LBrace, "expected '{' after materiallist")) {
            return materialNames;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("materialname")) {
                Advance();
                std::string name;
                ParseStringChunk(name, "expected '{' after materialname");
                if (!name.empty()) {
                    materialNames.push_back(std::move(name));
                }
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' after materiallist");
        return materialNames;
    }

    void ParseStringChunk(std::string& outText, const char* message) {
        if (!Expect(An8TokenKind::LBrace, message)) {
            return;
        }

        if (Check(An8TokenKind::String) || Check(An8TokenKind::Identifier)) {
            outText = current_.text;
            Advance();
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            SkipPropertyOrBlock();
        }
        Expect(An8TokenKind::RBrace, "expected '}' after string chunk");
    }

    void ParseRgbChunk(An8Color& outColor) {
        const std::vector<double> values = ParseNumberChunk(3, "expected '{' after rgb");
        if (values.size() >= 3) {
            outColor.r = ClampColorByte(values[0]);
            outColor.g = ClampColorByte(values[1]);
            outColor.b = ClampColorByte(values[2]);
            outColor.valid = true;
        }
    }

    void ParseBaseTransform(An8BaseTransform& outTransform) {
        if (!Expect(An8TokenKind::LBrace, "expected '{' after base")) {
            return;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("origin")) {
                Advance();
                if (ParseVector3Chunk(outTransform.origin, "expected '{' after origin")) {
                    outTransform.hasOrigin = true;
                }
            } else if (CheckIdentifier("orientation")) {
                Advance();
                if (ParseQuaternionChunk(outTransform.orientation, "expected '{' after orientation")) {
                    outTransform.hasOrientation = true;
                }
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' after base");
    }

    bool ParseQuaternionChunk(An8Quaternion& outQuaternion, const char* message) {
        if (!Expect(An8TokenKind::LBrace, message)) {
            return false;
        }

        std::vector<double> values;
        if (Check(An8TokenKind::LParen)) {
            Advance();
            while (!Check(An8TokenKind::End) &&
                   !Check(An8TokenKind::RParen) &&
                   values.size() < 4) {
                if (Check(An8TokenKind::Number)) {
                    values.push_back(current_.number);
                    Advance();
                } else {
                    SkipPropertyOrBlock();
                }
            }
            Expect(An8TokenKind::RParen, "expected ')' after orientation tuple");
        } else {
            while (!Check(An8TokenKind::End) &&
                   !Check(An8TokenKind::RBrace) &&
                   values.size() < 4) {
                if (Check(An8TokenKind::Number)) {
                    values.push_back(current_.number);
                    Advance();
                } else {
                    SkipPropertyOrBlock();
                }
            }
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            SkipPropertyOrBlock();
        }
        Expect(An8TokenKind::RBrace, "expected '}' after orientation");

        if (values.size() >= 4) {
            outQuaternion.x = static_cast<float>(values[0]);
            outQuaternion.y = static_cast<float>(values[1]);
            outQuaternion.z = static_cast<float>(values[2]);
            outQuaternion.w = static_cast<float>(values[3]);
            return true;
        }
        return false;
    }

    bool ParseVector3Chunk(An8Vector3& outVector, const char* message) {
        if (!Expect(An8TokenKind::LBrace, message)) {
            return false;
        }

        bool parsed = false;
        if (Check(An8TokenKind::LParen)) {
            outVector = ParseVector3();
            parsed = true;
        } else {
            std::vector<double> values;
            while (!Check(An8TokenKind::End) &&
                   !Check(An8TokenKind::RBrace) &&
                   values.size() < 3) {
                if (Check(An8TokenKind::Number)) {
                    values.push_back(current_.number);
                    Advance();
                } else {
                    SkipPropertyOrBlock();
                }
            }

            if (values.size() >= 3) {
                outVector.x = static_cast<float>(values[0]);
                outVector.y = static_cast<float>(values[1]);
                outVector.z = static_cast<float>(values[2]);
                parsed = true;
            }
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            SkipPropertyOrBlock();
        }
        Expect(An8TokenKind::RBrace, "expected '}' after vector chunk");
        return parsed;
    }

    std::vector<double> ParseNumberChunk(size_t maxValues, const char* message) {
        std::vector<double> values;
        if (!Expect(An8TokenKind::LBrace, message)) {
            return values;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (Check(An8TokenKind::Number)) {
                if (values.size() < maxValues) {
                    values.push_back(current_.number);
                }
                Advance();
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' after numeric chunk");
        return values;
    }

    static An8Face MakePrimitiveFace(std::initializer_list<uint32_t> indices) {
        An8Face face;
        face.indices.assign(indices.begin(), indices.end());
        return face;
    }

    static An8Mesh BuildCubePrimitive(const std::string& name, float sx, float sy, float sz) {
        const float hx = std::max(std::abs(sx) * 0.5f, 0.01f);
        const float hy = std::max(std::abs(sy) * 0.5f, 0.01f);
        const float hz = std::max(std::abs(sz) * 0.5f, 0.01f);

        An8Mesh mesh;
        mesh.name = name;
        mesh.points = {
            {-hx, -hy, -hz},
            { hx, -hy, -hz},
            { hx,  hy, -hz},
            {-hx,  hy, -hz},
            {-hx, -hy,  hz},
            { hx, -hy,  hz},
            { hx,  hy,  hz},
            {-hx,  hy,  hz}
        };
        mesh.faces = {
            MakePrimitiveFace({0, 1, 2, 3}),
            MakePrimitiveFace({4, 7, 6, 5}),
            MakePrimitiveFace({0, 4, 5, 1}),
            MakePrimitiveFace({1, 5, 6, 2}),
            MakePrimitiveFace({2, 6, 7, 3}),
            MakePrimitiveFace({3, 7, 4, 0})
        };
        return mesh;
    }

    static An8Mesh BuildSpherePrimitive(const std::string& name, float radius, int rings, int segments) {
        An8Mesh mesh;
        mesh.name = name;
        radius = std::max(radius, 0.01f);
        rings = std::max(3, rings);
        segments = std::max(6, segments);

        mesh.points.push_back({0.0f, radius, 0.0f});
        for (int r = 1; r < rings; ++r) {
            const float v = static_cast<float>(r) / static_cast<float>(rings);
            const float phi = v * AN8_PI;
            const float y = std::cos(phi) * radius;
            const float ringRadius = std::sin(phi) * radius;
            for (int s = 0; s < segments; ++s) {
                const float u = static_cast<float>(s) / static_cast<float>(segments);
                const float theta = u * 2.0f * AN8_PI;
                mesh.points.push_back({std::cos(theta) * ringRadius, y, std::sin(theta) * ringRadius});
            }
        }

        const uint32_t bottomIndex = static_cast<uint32_t>(mesh.points.size());
        mesh.points.push_back({0.0f, -radius, 0.0f});

        for (int s = 0; s < segments; ++s) {
            mesh.faces.push_back(MakePrimitiveFace({0, static_cast<uint32_t>(1 + s), static_cast<uint32_t>(1 + ((s + 1) % segments))}));
        }

        for (int r = 0; r < rings - 2; ++r) {
            const uint32_t row0 = static_cast<uint32_t>(1 + r * segments);
            const uint32_t row1 = static_cast<uint32_t>(1 + (r + 1) * segments);
            for (int s = 0; s < segments; ++s) {
                mesh.faces.push_back(MakePrimitiveFace({
                    row0 + static_cast<uint32_t>(s),
                    row0 + static_cast<uint32_t>((s + 1) % segments),
                    row1 + static_cast<uint32_t>((s + 1) % segments),
                    row1 + static_cast<uint32_t>(s)
                }));
            }
        }

        const uint32_t lastRow = static_cast<uint32_t>(1 + (rings - 2) * segments);
        for (int s = 0; s < segments; ++s) {
            mesh.faces.push_back(MakePrimitiveFace({lastRow + static_cast<uint32_t>((s + 1) % segments), lastRow + static_cast<uint32_t>(s), bottomIndex}));
        }
        return mesh;
    }

    static An8Mesh BuildCylinderPrimitive(const std::string& name, float topRadius, float bottomRadius, float height, int segments, bool capStart, bool capEnd) {
        An8Mesh mesh;
        mesh.name = name;
        topRadius = std::max(topRadius, 0.0f);
        bottomRadius = std::max(bottomRadius, 0.0f);
        height = std::max(height, 0.01f);
        segments = std::max(3, segments);
        const float half = height * 0.5f;

        for (int i = 0; i < segments; ++i) {
            const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * AN8_PI;
            mesh.points.push_back({std::cos(a) * bottomRadius, -half, std::sin(a) * bottomRadius});
        }
        for (int i = 0; i < segments; ++i) {
            const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * AN8_PI;
            mesh.points.push_back({std::cos(a) * topRadius, half, std::sin(a) * topRadius});
        }

        for (int i = 0; i < segments; ++i) {
            const uint32_t b0 = static_cast<uint32_t>(i);
            const uint32_t b1 = static_cast<uint32_t>((i + 1) % segments);
            const uint32_t t1 = static_cast<uint32_t>(segments + ((i + 1) % segments));
            const uint32_t t0 = static_cast<uint32_t>(segments + i);
            mesh.faces.push_back(MakePrimitiveFace({b0, b1, t1, t0}));
        }

        if (capStart && bottomRadius > 0.0001f) {
            An8Face bottom;
            for (int i = 0; i < segments; ++i) {
                bottom.indices.push_back(static_cast<uint32_t>(segments - 1 - i));
            }
            mesh.faces.push_back(std::move(bottom));
        }

        if (capEnd && topRadius > 0.0001f) {
            An8Face top;
            for (int i = 0; i < segments; ++i) {
                top.indices.push_back(static_cast<uint32_t>(segments + i));
            }
            mesh.faces.push_back(std::move(top));
        }

        return mesh;
    }

    static int ClampColorByte(double value) {
        return std::clamp(static_cast<int>(value), 0, 255);
    }

    static std::string ToLowerAscii(std::string text) {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    static An8Vector3 RotateVector(const An8Vector3& point, An8Quaternion q) {
        const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length <= 0.00001f) {
            return point;
        }

        q.x /= length;
        q.y /= length;
        q.z /= length;
        q.w /= length;

        const An8Vector3 u{q.x, q.y, q.z};
        const float s = q.w;
        const auto dot = [](const An8Vector3& a, const An8Vector3& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        };
        const auto cross = [](const An8Vector3& a, const An8Vector3& b) {
            return An8Vector3{
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        };

        const An8Vector3 c = cross(u, point);
        const float udot = dot(u, point);
        const float ulen2 = dot(u, u);
        return {
            2.0f * udot * u.x + (s * s - ulen2) * point.x + 2.0f * s * c.x,
            2.0f * udot * u.y + (s * s - ulen2) * point.y + 2.0f * s * c.y,
            2.0f * udot * u.z + (s * s - ulen2) * point.z + 2.0f * s * c.z
        };
    }

    static void ApplyBaseTransform(An8Mesh& mesh, const An8BaseTransform& transform) {
        for (An8Vector3& point : mesh.points) {
            if (transform.hasOrientation) {
                point = RotateVector(point, transform.orientation);
            }
            if (transform.hasOrigin) {
                point.x += transform.origin.x;
                point.y += transform.origin.y;
                point.z += transform.origin.z;
            }
        }
    }

    void ParseNameProperty(std::string& outName) {
        Advance(); // name

        if (Match(An8TokenKind::LBrace)) {
            if (Check(An8TokenKind::String)) {
                outName = current_.text;
                Advance();
            }
            while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
                SkipPropertyOrBlock();
            }
            Expect(An8TokenKind::RBrace, "expected '}' after name block");
            return;
        }

        if (Check(An8TokenKind::String)) {
            outName = current_.text;
            Advance();
            return;
        }

        SkipPropertyOrBlock();
    }

    std::vector<An8Vector3> ParsePoints() {
        std::vector<An8Vector3> points;
        if (!Expect(An8TokenKind::LBrace, "expected '{' after points")) {
            return points;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (Check(An8TokenKind::LParen)) {
                points.push_back(ParseVector3());
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close points");
        return points;
    }

    std::vector<An8Vector2> ParseTexcoords() {
        std::vector<An8Vector2> texcoords;
        if (!Expect(An8TokenKind::LBrace, "expected '{' after texcoords")) {
            return texcoords;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (Check(An8TokenKind::LParen)) {
                texcoords.push_back(ParseVector2());
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close texcoords");
        return texcoords;
    }

    An8Vector3 ParseVector3() {
        An8Vector3 v;
        Expect(An8TokenKind::LParen, "expected '(' before vector");
        v.x = ReadFloat("expected vector x value");
        v.y = ReadFloat("expected vector y value");
        v.z = ReadFloat("expected vector z value");
        Expect(An8TokenKind::RParen, "expected ')' after vector");
        return v;
    }

    An8Vector2 ParseVector2() {
        An8Vector2 v;
        Expect(An8TokenKind::LParen, "expected '(' before texcoord");
        v.u = ReadFloat("expected texcoord u value");
        v.v = ReadFloat("expected texcoord v value");
        Expect(An8TokenKind::RParen, "expected ')' after texcoord");
        return v;
    }

    std::vector<An8Face> ParseFaces() {
        std::vector<An8Face> faces;
        if (!Expect(An8TokenKind::LBrace, "expected '{' after faces")) {
            return faces;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (!Check(An8TokenKind::Number)) {
                SkipPropertyOrBlock();
                continue;
            }

            const int expectedCount = ReadInt("expected face vertex count");
            std::vector<int> faceHeader;

            // Anim8or face rows commonly start as:
            //   vertexCount flags material smoothGroup (i0 i1 i2 ...)
            // We preserve only the index tuple for geometry.
            while (!Check(An8TokenKind::End) &&
                   !Check(An8TokenKind::RBrace) &&
                   !Check(An8TokenKind::LParen)) {
                if (Check(An8TokenKind::Number)) {
                    faceHeader.push_back(static_cast<int>(current_.number));
                }
                Advance();
            }

            if (!Match(An8TokenKind::LParen)) {
                Error("expected '(' before face indices");
                continue;
            }

            An8Face face = ParseFacePointData();
            if (faceHeader.size() >= 2) {
                face.materialIndex = faceHeader[1];
            }

            Expect(An8TokenKind::RParen, "expected ')' after face indices");

            if (expectedCount > 0 && static_cast<int>(face.indices.size()) != expectedCount) {
                Warning("face index count does not match declared vertex count");
            }

            if (face.indices.size() >= 3) {
                faces.push_back(std::move(face));
            } else {
                Warning("face with fewer than 3 indices ignored");
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close faces");
        return faces;
    }

    An8Face ParseFacePointData() {
        An8Face face;

        if (Check(An8TokenKind::LParen)) {
            while (!Check(An8TokenKind::End) &&
                   !Check(An8TokenKind::RBrace) &&
                   Check(An8TokenKind::LParen)) {
                Advance(); // point-data tuple

                int pointIndex = -1;
                int texcoordIndex = -1;
                int numericSlot = 0;
                while (!Check(An8TokenKind::End) &&
                       !Check(An8TokenKind::RBrace) &&
                       !Check(An8TokenKind::RParen)) {
                    if (Check(An8TokenKind::Number)) {
                        const int index = ReadInt("expected point-data index");
                        if (numericSlot == 0) {
                            pointIndex = index;
                        } else if (numericSlot == 1) {
                            texcoordIndex = index;
                        }
                        ++numericSlot;
                    } else if (Check(An8TokenKind::LParen)) {
                        SkipBalancedParens();
                    } else {
                        Advance();
                    }
                }

                Expect(An8TokenKind::RParen, "expected ')' after face point-data tuple");
                if (pointIndex >= 0) {
                    face.indices.push_back(static_cast<uint32_t>(pointIndex));
                    face.texcoordIndices.push_back(texcoordIndex >= 0
                        ? static_cast<uint32_t>(texcoordIndex)
                        : UINT32_MAX);
                } else {
                    Warning("negative face point index ignored");
                }
            }

            return face;
        }

        while (!Check(An8TokenKind::End) &&
               !Check(An8TokenKind::RBrace) &&
               !Check(An8TokenKind::RParen)) {
            if (Check(An8TokenKind::Number)) {
                const int index = ReadInt("expected face index");
                if (index >= 0) {
                    face.indices.push_back(static_cast<uint32_t>(index));
                    face.texcoordIndices.push_back(UINT32_MAX);
                } else {
                    Warning("negative face index ignored");
                }
            } else if (Check(An8TokenKind::LParen)) {
                SkipBalancedParens();
            } else {
                Advance();
            }
        }

        return face;
    }

    float ReadFloat(const char* message) {
        if (!Check(An8TokenKind::Number)) {
            Error(message);
            return 0.0f;
        }
        const float value = static_cast<float>(current_.number);
        Advance();
        return value;
    }

    int ReadInt(const char* message) {
        if (!Check(An8TokenKind::Number)) {
            Error(message);
            return 0;
        }
        const int value = static_cast<int>(current_.number);
        Advance();
        return value;
    }

    void SkipTopLevelEntry() {
        Advance();
        if (Check(An8TokenKind::LBrace)) {
            SkipBalancedBlock();
        }
    }

    void SkipPropertyOrBlock() {
        if (Check(An8TokenKind::LBrace)) {
            SkipBalancedBlock();
            return;
        }
        if (Check(An8TokenKind::LParen)) {
            SkipBalancedParens();
            return;
        }

        Advance();

        if (Check(An8TokenKind::LBrace)) {
            SkipBalancedBlock();
        } else if (Check(An8TokenKind::LParen)) {
            SkipBalancedParens();
        }
    }

    void SkipBalancedBlock() {
        if (!Check(An8TokenKind::LBrace)) {
            return;
        }

        int depth = 0;
        do {
            if (Check(An8TokenKind::LBrace)) {
                ++depth;
            } else if (Check(An8TokenKind::RBrace)) {
                --depth;
            }
            Advance();
        } while (depth > 0 && !Check(An8TokenKind::End));
    }

    void SkipBalancedParens() {
        if (!Check(An8TokenKind::LParen)) {
            return;
        }

        int depth = 0;
        do {
            if (Check(An8TokenKind::LParen)) {
                ++depth;
            } else if (Check(An8TokenKind::RParen)) {
                --depth;
            }
            Advance();
        } while (depth > 0 && !Check(An8TokenKind::End));
    }
};

inline An8Geometry BuildTriangleGeometry(const An8Mesh& mesh, std::vector<std::string>* warnings = nullptr) {
    An8Geometry geometry;
    geometry.vertices.reserve(mesh.points.size());

    for (const An8Vector3& p : mesh.points) {
        geometry.vertices.push_back({p});
    }

    for (const An8Face& face : mesh.faces) {
        bool valid = true;
        for (uint32_t index : face.indices) {
            if (index >= mesh.points.size()) {
                valid = false;
                if (warnings) {
                    warnings->push_back("face references point index outside mesh point array");
                }
                break;
            }
        }

        if (!valid || face.indices.size() < 3) {
            continue;
        }

        // Fan triangulation. Quads become two triangles: 0-1-2 and 0-2-3.
        // N-gons are accepted for low-poly imports, but later tools should offer
        // more robust triangulation for concave polygons.
        for (size_t i = 1; i + 1 < face.indices.size(); ++i) {
            geometry.indices.push_back(face.indices[0]);
            geometry.indices.push_back(face.indices[i]);
            geometry.indices.push_back(face.indices[i + 1]);
        }
    }

    return geometry;
}

inline An8LoadResult LoadAn8File(const std::string& filePath) {
    An8LoadResult result;

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        result.errors.push_back("failed to open .an8 file: " + filePath);
        return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    An8Parser parser(buffer.str());
    result.document = parser.ParseDocument();
    result.errors = parser.Errors();
    result.warnings = parser.Warnings();
    result.ok = result.errors.empty();

    if (result.document.objects.empty()) {
        result.warnings.push_back("no object blocks were found");
    }

    return result;
}

} // namespace anim8orx
