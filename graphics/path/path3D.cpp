#include "core.h"

#include "path3D.h"

#include "../physics/math/mathUtil.h"

namespace Path3D {

	//! Batch

	Batch<Vertex>* batch = nullptr;

	void bind(Batch<Vertex>* batch) {
		Path3D::batch = batch;
	}

	// Adds the vertices to the batch with the necessary indices, this does not reserve space on the batch
	void pushLine(const Vec3f& a, const Vec3f& b, const Vec4f& colorA, const Vec4f& colorB, float thickness) {
		Path3D::batch->pushVertices({ { a, colorA }, { b, colorB } });
		Path3D::batch->pushIndices({ 0, 1 });
		Path3D::batch->endIndex();
	}

	//! Primitives

	void line(const Vec3f& a, const Vec3f& b, float thickness, const Vec4f& colorA, const Vec4f& colorB) {
		size_t vertexCount = 4;
		size_t indexCount = 6;
		Path3D::batch->reserve(vertexCount, indexCount);

		pushLine(a, b, colorA, colorB, thickness);
	}

	void circle(const Vec3f& center, float radius, const Vec3f& normal, float thickness, const Vec4f& color, size_t precision) {
		Vec3f n = normalize(normal);
		Vec3f u = Vec3f(n.y, -n.x, 0);

		if (lengthSquared(u) == 0)
			u = Vec3f(n.z, 0, -n.x);
	
		u = normalize(u);
		Vec3f v = n % u;

		size_t vertexCount = precision;
		size_t indexCount = 2 * precision;

		Path3D::batch->reserve(vertexCount, indexCount);
		Vec3f point = center + radius * u;
		Path3D::batch->pushVertex({ point, color });

		float step = 2.0 * PI / (float) precision;
		for (size_t i = 1; i < precision; i++) {
			float angle = i * step;
			point = center + radius * (cos(angle) * u + sin(angle) * v);

			Path3D::batch->pushVertex({ point, color });
			Path3D::batch->pushIndices({ i - 1, i });
		}
		
		Path3D::batch->pushIndices({ precision - 1, 0 });
		Path3D::batch->endIndex();
	}

	void triangle(const Vec3f& a, const Vec3f& b, const Vec3f& c, float thickness, const Vec4f& colorA, const Vec4f& colorB, const Vec4f& colorC) {
		size_t vertexCount = 3;
		size_t indexCount = 6;

		Path3D::batch->pushVertices({ { a, colorA }, { b, colorB }, { c, colorC } });
		Path3D::batch->pushIndices({ 0, 1, 1, 2, 2, 0 });

		Path3D::batch->endIndex();
	}

	void polyLine(Vec3f* points, size_t size, float thickness, const Vec4f& color, bool closed) {
		if (size == 0)
			return;

		if (size == 1)
			return; // Points not supported yet

		size_t vertexCount = size;
		size_t indexCount = 2 * (size - (closed ? 0 : 1));

		Path3D::batch->reserve(vertexCount, indexCount);

		Path3D::batch->pushVertex({ points[0], color });
		
		for (size_t i = 1; i < size; i++) {
			Path3D::batch->pushVertex({ points[i], color });
			Path3D::batch->pushIndices({ i - 1, i });
		}

		if (closed) 
			Path3D::batch->pushIndices({ size - 1, 0 });
		

		Path3D::batch->endIndex();
	}

	//! Path

	std::vector<Vec3f> path;

	void lineTo(const Vec3f& vertex) {
		path.push_back(vertex);
	}

	void bezierTo(const Vec3f& end, const Vec3f& tc1, const Vec3f& tc2, size_t precision) {
		Vec3f start = path.back();

		float step = 1.0f / (float)precision;
		for (size_t i = 1; i <= precision; i++) {
			float t = i * step;
			float s = 1 - t;

			float w1 = s * s * s;
			float w2 = 3.0f * s * s * t;
			float w3 = 3.0f * s * t * t;
			float w4 = t * t * t;

			path.push_back(w1 * start + w2 * tc1 + w3 * tc2 + w4 * end);
		}
	}

	void bezierTo(const Vec3f& end, size_t precision) {
		Vec3f start = path.back();

		float midX = (start.y + end.y) / 2.0f;
		float midZ = (start.y + end.y) / 2.0f;
		Vec3f c1 = Vec3f(midX, start.y, midZ);
		Vec3f c2 = Vec3f(midX, end.y, midZ);

		bezierTo(end, c1, c2, precision);
	}

	void clear() {
		path.clear();
	}

	void stroke(Vec4f color, float thickness, bool closed) {
		polyLine(path.data(), path.size(), thickness, color, closed);

		clear();
	}

}