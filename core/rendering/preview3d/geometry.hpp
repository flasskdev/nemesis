#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace nemesis::preview3d {
struct vec2 { float x{}, y{}; };
struct vec3 { float x{}, y{}, z{}; };
inline vec3 operator+(vec3 a, vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline vec3 operator-(vec3 a, vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline vec3 operator*(vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline vec3 operator/(vec3 a, float s) { return a*(1.0f/s); }
inline float dot(vec3 a, vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline vec3 cross(vec3 a, vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(vec3 v) { return std::sqrt(dot(v,v)); }
inline vec3 unit(vec3 v) { const float n=length(v); return n>1e-12f ? v/n : vec3{0,1,0}; }
inline bool finite(vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
struct vertex { vec3 position; vec3 normal; vec2 uv; };
static_assert(sizeof(vertex)==32);
struct mesh {
    std::vector<vertex> vertices;
    vec3 center{};
    float radius{1};
    std::uint64_t fingerprint{};
};
struct surface_anchor {
    std::uint32_t triangle{};
    float u{}, v{}; // Weights of triangle corners 1 and 2; corner 0 = 1-u-v.
};
struct hit { surface_anchor anchor; float distance{}; };

inline std::optional<hit> raycast(const mesh& m, vec3 origin, vec3 direction) {
    std::optional<hit> best;
    for (std::size_t i=0;i+2<m.vertices.size();i+=3) {
        const vec3 a=m.vertices[i].position;
        const vec3 ab=m.vertices[i+1].position-a, ac=m.vertices[i+2].position-a;
        const vec3 h=cross(direction,ac);
        const float determinant=dot(ab,h);
        if (std::abs(determinant)<1e-9f) continue;
        const float inv=1.0f/determinant;
        const vec3 s=origin-a;
        const float u=inv*dot(s,h);
        if (u<0 || u>1) continue;
        const vec3 q=cross(s,ab);
        const float v=inv*dot(direction,q);
        if (v<0 || u+v>1) continue;
        const float d=inv*dot(ac,q);
        if (d>1e-5f && (!best || d<best->distance))
            best=hit{{static_cast<std::uint32_t>(i/3),u,v},d};
    }
    return best;
}
inline vec3 anchor_position(const mesh& m, surface_anchor a) {
    const auto i=static_cast<std::size_t>(a.triangle)*3;
    return m.vertices.at(i).position*(1-a.u-a.v)+m.vertices.at(i+1).position*a.u+m.vertices.at(i+2).position*a.v;
}
inline vec3 anchor_normal(const mesh& m, surface_anchor a) {
    const auto i=static_cast<std::size_t>(a.triangle)*3;
    return unit(m.vertices.at(i).normal*(1-a.u-a.v)+m.vertices.at(i+1).normal*a.u+m.vertices.at(i+2).normal*a.v);
}
struct camera {
    float yaw{}, pitch{0.12f}, distance{3.2f};
    static constexpr float tangent=0.41421356237f; // 45 degree vertical field of view.
    vec3 eye() const { return {std::sin(yaw)*std::cos(pitch)*distance,std::sin(pitch)*distance,-std::cos(yaw)*std::cos(pitch)*distance}; }
    vec3 forward() const { return unit(eye()*-1.0f); }
    vec3 right() const { return unit(cross({0,1,0},forward())); }
    vec3 up() const { return cross(forward(),right()); }
    vec3 ray(float nx, float ny, float aspect) const { return unit(forward()+right()*(nx*aspect*tangent)+up()*(ny*tangent)); }
    std::optional<vec2> project(vec3 p, float aspect) const {
        const auto v=p-eye(); const float z=dot(v,forward());
        if (z<=0.02f || z>=100) return std::nullopt;
        return vec2{dot(v,right())/(z*aspect*tangent),dot(v,up())/(z*tangent)};
    }
};

// Strict, bounded OBJ parsing. Triangles and planar convex polygons are accepted.
// Concave faces must be triangulated by the exporter, never silently fan-filled.
inline mesh read_obj(std::istream& input) {
    constexpr std::size_t max_points=600000, max_vertices=600000;
    constexpr std::size_t max_bytes=32*1024*1024;
    mesh result;
    std::vector<vec3> positions,normals;
    std::vector<vec2> uvs;
    std::string line;
    std::size_t line_number=0, total=0;
    std::uint64_t fingerprint=14695981039346656037ull;
    auto fail=[&](const std::string& msg) { throw std::runtime_error("OBJ line "+std::to_string(line_number)+": "+msg); };
    auto number=[&](std::istringstream& s) {
        float v{};
        if (!(s>>v) || !std::isfinite(v) || std::abs(v)>1e9f) fail("invalid coordinate");
        return v;
    };
    auto index=[&](const std::string& token,std::size_t count) -> std::size_t {
        std::size_t used=0;
        const long long value=std::stoll(token,&used);
        if (used!=token.size() || value==0) fail("invalid face index");
        const long long at=value>0 ? value-1 : static_cast<long long>(count)+value;
        if (at<0 || at>=static_cast<long long>(count)) fail("face index out of range");
        return static_cast<std::size_t>(at);
    };
    while (std::getline(input,line)) {
        ++line_number; total+=line.size()+1;
        if (total>max_bytes || line.size()>65536) fail("file or line limit exceeded");
        for (unsigned char c:line) { fingerprint^=c; fingerprint*=1099511628211ull; }
        fingerprint^=10; fingerprint*=1099511628211ull;
        if (line_number==1 && line.starts_with("\xef\xbb\xbf")) line.erase(0,3);
        std::istringstream s(line); s.imbue(std::locale::classic());
        std::string kind; s>>kind;
        if (kind.empty() || kind[0]=='#') continue;
        if (kind=="v" || kind=="vn") {
            const vec3 v{number(s),number(s),number(s)};
            auto& list=kind=="v" ? positions : normals;
            if (list.size()>=max_points) fail("too many points");
            if (kind=="vn" && length(v)<1e-12f) fail("zero normal");
            list.push_back(kind=="vn" ? unit(v) : v);
        } else if (kind=="vt") {
            const vec2 uv{number(s),number(s)};
            if (uvs.size()>=max_points) fail("too many UV coordinates");
            uvs.push_back(uv);
        } else if (kind=="f") {
            std::vector<vertex> face;
            std::vector<bool> has_normal;
            std::string token;
            while (s>>token && token[0]!='#') {
                if (face.size()>=64) fail("face has more than 64 corners; triangulate it");
                std::array<std::string,3> fields{};
                std::size_t start=0,field=0;
                for (;;) {
                    const auto slash=token.find('/',start);
                    if (field>=fields.size()) fail("invalid face token");
                    fields[field++]=token.substr(start,slash==std::string::npos ? slash : slash-start);
                    if (slash==std::string::npos) break;
                    start=slash+1;
                }
                vertex v{};
                try {
                    v.position=positions.at(index(fields[0],positions.size()));
                    if (!fields[1].empty()) v.uv=uvs.at(index(fields[1],uvs.size()));
                    if (!fields[2].empty()) v.normal=normals.at(index(fields[2],normals.size()));
                } catch (const std::exception&) { fail("invalid or out-of-range face reference"); }
                face.push_back(v); has_normal.push_back(!fields[2].empty());
            }
            if (face.size()<3) fail("face has fewer than three corners");
            vec3 face_normal{};
            for (std::size_t j=1;j+1<face.size();++j)
                face_normal=face_normal+cross(face[j].position-face[0].position,face[j+1].position-face[0].position);
            if (length(face_normal)<1e-12f) fail("degenerate face");
            face_normal=unit(face_normal);
            float extent=0;
            for (const auto& v:face) extent=std::max(extent,length(v.position-face[0].position));
            for (std::size_t j=0;j<face.size();++j) {
                const auto a=face[j].position,b=face[(j+1)%face.size()].position,c=face[(j+2)%face.size()].position;
                if (std::abs(dot(a-face[0].position,face_normal))>std::max(1e-6f,extent*1e-4f))
                    fail("non-planar face; triangulate before export");
                if (dot(cross(b-a,c-b),face_normal)<-1e-7f*extent*extent)
                    fail("concave face; triangulate before export");
            }
            for (std::size_t j=1;j+1<face.size();++j) {
                if (result.vertices.size()+3>max_vertices) fail("more than 200000 triangles");
                if (length(cross(face[j].position-face[0].position,face[j+1].position-face[0].position))<1e-12f) continue;
                for (const std::size_t k: {std::size_t{0},j,j+1}) {
                    auto v=face[k]; if (!has_normal[k]) v.normal=face_normal;
                    result.vertices.push_back(v);
                }
            }
        }
    }
    if (input.bad()) fail("file read failed");
    if (result.vertices.empty()) fail("no renderable faces");
    vec3 lo=result.vertices.front().position,hi=lo;
    for (const auto& v:result.vertices) {
        lo={std::min(lo.x,v.position.x),std::min(lo.y,v.position.y),std::min(lo.z,v.position.z)};
        hi={std::max(hi.x,v.position.x),std::max(hi.y,v.position.y),std::max(hi.z,v.position.z)};
    }
    result.center=(lo+hi)*0.5f; result.radius=0;
    for (const auto& v:result.vertices) result.radius=std::max(result.radius,length(v.position-result.center));
    if (!std::isfinite(result.radius) || result.radius<1e-9f) fail("zero or invalid model size");
    for (auto& v:result.vertices) v.position=(v.position-result.center)/result.radius;
    result.fingerprint=fingerprint;
    return result;
}
}
