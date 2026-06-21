#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace anim8orx {

struct An8Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct An8Face {
    std::vector<uint32_t> indices;
};

struct An8Mesh {
    std::string name;
    std::vector<An8Vector3> points;
    std::vector<An8Face> faces;
};

struct An8Object {
    std::string name;
    std::vector<An8Mesh> meshes;
    std::vector<An8Object> children;
};

struct An8Document {
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
            if (CheckIdentifier("mesh")) {
                Advance();
                An8Mesh mesh = ParseMesh();
                if (mesh.name.empty()) {
                    mesh.name = object.name.empty()
                        ? "Mesh_" + std::to_string(object.meshes.size())
                        : object.name + "_Mesh_" + std::to_string(object.meshes.size());
                }
                object.meshes.push_back(std::move(mesh));
            } else if (CheckIdentifier("object")) {
                Advance();
                An8Object child = ParseObject();
                if (child.name.empty()) {
                    child.name = "Child_" + std::to_string(object.children.size());
                }
                object.children.push_back(std::move(child));
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

    An8Mesh ParseMesh() {
        An8Mesh mesh;

        if (Check(An8TokenKind::String)) {
            mesh.name = current_.text;
            Advance();
        }

        if (!Expect(An8TokenKind::LBrace, "expected '{' after mesh")) {
            return mesh;
        }

        while (!Check(An8TokenKind::End) && !Check(An8TokenKind::RBrace)) {
            if (CheckIdentifier("name")) {
                ParseNameProperty(mesh.name);
            } else if (CheckIdentifier("points")) {
                Advance();
                mesh.points = ParsePoints();
            } else if (CheckIdentifier("faces")) {
                Advance();
                mesh.faces = ParseFaces();
            } else {
                SkipPropertyOrBlock();
            }
        }

        Expect(An8TokenKind::RBrace, "expected '}' to close mesh");
        return mesh;
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

    An8Vector3 ParseVector3() {
        An8Vector3 v;
        Expect(An8TokenKind::LParen, "expected '(' before vector");
        v.x = ReadFloat("expected vector x value");
        v.y = ReadFloat("expected vector y value");
        v.z = ReadFloat("expected vector z value");
        Expect(An8TokenKind::RParen, "expected ')' after vector");
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

            // Anim8or face rows commonly start as:
            //   vertexCount flags material smoothGroup (i0 i1 i2 ...)
            // We preserve only the index tuple for geometry.
            while (!Check(An8TokenKind::End) &&
                   !Check(An8TokenKind::RBrace) &&
                   !Check(An8TokenKind::LParen)) {
                Advance();
            }

            if (!Match(An8TokenKind::LParen)) {
                Error("expected '(' before face indices");
                continue;
            }

            An8Face face;
            while (!Check(An8TokenKind::End) &&
                   !Check(An8TokenKind::RBrace) &&
                   !Check(An8TokenKind::RParen)) {
                if (Check(An8TokenKind::Number)) {
                    const int index = ReadInt("expected face index");
                    if (index >= 0) {
                        face.indices.push_back(static_cast<uint32_t>(index));
                    } else {
                        Warning("negative face index ignored");
                    }
                } else {
                    Advance();
                }
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
