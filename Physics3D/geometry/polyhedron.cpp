#include "polyhedron.h"

#include "../math/linalg/vec.h"
#include "../math/utils.h"
#include "../misc/debug.h"
#include "../misc/validityHelper.h"

namespace P3D {
Polyhedron::Polyhedron(const Vec3f* vertices, const Triangle* triangles, int vertexCount, int triangleCount) :
	TriangleMesh(vertexCount, triangleCount, vertices, triangles) {
	assert(isValid(*this));
}
Polyhedron::Polyhedron(const TriangleMesh& mesh) :
	TriangleMesh(mesh) {
	assert(isValid(*this));
}
Polyhedron::Polyhedron(TriangleMesh&& mesh) noexcept :
	TriangleMesh(std::move(mesh)) {
	assert(isValid(*this));
}
Polyhedron::Polyhedron(const MeshPrototype& mesh) :
	TriangleMesh(mesh) {
	assert(isValid(*this));
}
Polyhedron::Polyhedron(MeshPrototype&& mesh) noexcept :
	TriangleMesh(std::move(mesh)) {
	assert(isValid(*this));
}


Polyhedron Polyhedron::translated(Vec3f offset) const { return Polyhedron(TriangleMesh::translated(offset)); }
Polyhedron Polyhedron::rotated(Rotationf rotation) const { return Polyhedron(TriangleMesh::rotated(rotation)); }
Polyhedron Polyhedron::localToGlobal(CFramef frame) const { return Polyhedron(TriangleMesh::localToGlobal(frame)); }
Polyhedron Polyhedron::globalToLocal(CFramef frame) const { return Polyhedron(TriangleMesh::globalToLocal(frame)); }
Polyhedron Polyhedron::scaled(float scaleX, float scaleY, float scaleZ) const { return Polyhedron(TriangleMesh::scaled(scaleX, scaleY, scaleZ)); }
Polyhedron Polyhedron::scaled(DiagonalMat3f scale) const { return Polyhedron(TriangleMesh::scaled(scale)); }
Polyhedron Polyhedron::translatedAndScaled(Vec3f translation, DiagonalMat3f scale) const { return Polyhedron(TriangleMesh::translatedAndScaled(translation, scale)); }

//TODO parallelize
bool Polyhedron::containsPoint(Vec3f point) const {
	Vec3f ray(1, 0, 0);

	bool isExiting = false;
	double bestD = std::numeric_limits<double>::infinity();

	for(const Triangle& tri : iterTriangles()) {
		RayIntersection<float> r = rayTriangleIntersection(point, ray, this->getVertex(tri.firstIndex), this->getVertex(tri.secondIndex), this->getVertex(tri.thirdIndex));
		if(r.d >= 0 && r.lineIntersectsTriangle()) {
			if(r.d < bestD) {
				bestD = r.d;
				isExiting = (getNormalVecOfTriangle(tri) * ray >= 0);
			}
		}
	}

	return isExiting;
}

double Polyhedron::getVolume() const {
	double total = 0;
	for(Triangle triangle : iterTriangles()) {
		Vec3 v0 = this->getVertex(triangle.firstIndex);
		Vec3 v1 = this->getVertex(triangle.secondIndex);
		Vec3 v2 = this->getVertex(triangle.thirdIndex);

		double D1x = v1.x - v0.x;
		double D1y = v1.y - v0.y;
		double D2x = v2.x - v0.x;
		double D2y = v2.y - v0.y;

		double nz = D1x * D2y - D1y * D2x;

		total += nz * (v0.z + v1.z + v2.z);
	}
	if(((total / 6)<=0)){
	  printf("");
	}
	
	return total / 6;
}

Vec3 Polyhedron::getCenterOfMass() const {
	Vec3 total(0, 0, 0);
	for(Triangle triangle : iterTriangles()) {
		Vec3 v0 = this->getVertex(triangle.firstIndex);
		Vec3 v1 = this->getVertex(triangle.secondIndex);
		Vec3 v2 = this->getVertex(triangle.thirdIndex);

		Vec3 normalVec = (v1 - v0) % (v2 - v0);
		Vec3 vFactor = elementWiseSquare(v0) + elementWiseSquare(v1) + elementWiseSquare(v2) + elementWiseMul(v0, v1) + elementWiseMul(v1, v2) + elementWiseMul(v2, v0);

		total += elementWiseMul(normalVec, vFactor);
	}

	return total / (24 * getVolume());
}

/*
	The total inertial matrix is given by the integral over the volume of the shape of the following matrix:
	[[
	[y^2+z^2,    xy,    xz],
	[xy,    x^2+z^2,    yz],
	[xz,    yz,    x^2+y^2]
	]]

	This has been reworked to a surface integral resulting in the given formulae

	This function returns an intermediary step which can easily be converted to scaled versions of the inertial matrix
*/
ScalableInertialMatrix Polyhedron::getScalableInertia(const CFrame& reference) const {
	Vec3 totalDiagElementParts(0, 0, 0);
	Vec3 totalOffDiag(0, 0, 0);
	for(Triangle triangle : iterTriangles()) {
		Vec3 v0 = reference.globalToLocal(Vec3(this->getVertex(triangle.firstIndex)));
		Vec3 v1 = reference.globalToLocal(Vec3(this->getVertex(triangle.secondIndex)));
		Vec3 v2 = reference.globalToLocal(Vec3(this->getVertex(triangle.thirdIndex)));

		Vec3 normalVec = (v1 - v0) % (v2 - v0);  // scales x: sy*sz,  y:  sx*sz    z: sx*sy

		// Diagonal Elements      // sx*sx*sx,  sy*sy*sy,  sz*sz*sz
		Vec3 squaredIntegral = elementWiseCube(v0) + elementWiseCube(v1) + elementWiseCube(v2) + elementWiseMul(elementWiseSquare(v0), v1 + v2) + elementWiseMul(elementWiseSquare(v1), v0 + v2) + elementWiseMul(elementWiseSquare(v2), v0 + v1) + elementWiseMul(elementWiseMul(v0, v1), v2);
		Vec3 diagonalElementParts = elementWiseMul(normalVec, squaredIntegral); // (sx^3)*sy*sz, sx*(sy^3)*sz, sx*sy*(sz^3)

		totalDiagElementParts += diagonalElementParts;

		//total[0][0] += diagonalElementParts.y + diagonalElementParts.z; // sx*sy*sz*(sy^2+sz^2)
		//total[1][1] += diagonalElementParts.z + diagonalElementParts.x; // sx*sy*sz*(sz^2+sx^2)
		//total[2][2] += diagonalElementParts.x + diagonalElementParts.y; // sx*sy*sz*(sx^2+sy^2)

		// Other Elements
		double selfProducts = v0.x * v0.y * v0.z + v1.x * v1.y * v1.z + v2.x * v2.y * v2.z;
		double twoSames = v0.x * v0.y * v1.z + v0.x * v1.y * v0.z + v0.x * v1.y * v1.z + v0.x * v0.y * v2.z + v0.x * v2.y * v0.z + v0.x * v2.y * v2.z +
			v1.x * v0.y * v0.z + v1.x * v1.y * v0.z + v1.x * v0.y * v1.z + v1.x * v1.y * v2.z + v1.x * v2.y * v1.z + v1.x * v2.y * v2.z +
			v2.x * v0.y * v0.z + v2.x * v1.y * v2.z + v2.x * v0.y * v2.z + v2.x * v1.y * v1.z + v2.x * v2.y * v0.z + v2.x * v2.y * v1.z;
		double allDifferents = v0.x * v1.y * v2.z + v0.x * v2.y * v1.z + v1.x * v0.y * v2.z + v1.x * v2.y * v0.z + v2.x * v0.y * v1.z + v2.x * v1.y * v0.z;

		double xyzIntegral = -(3.0 * selfProducts + twoSames + 0.5 * allDifferents); // scales linearly by sx*sy*sz

		totalOffDiag += normalVec * xyzIntegral;

		//total[1][0] += dFactor.z * xyzIntegral; // sx*sy*sz* sx*sy
		//total[2][0] += dFactor.y * xyzIntegral; // sx*sy*sz* sx*sz
		//total[2][1] += dFactor.x * xyzIntegral; // sx*sy*sz* sz*sy
	}

	return ScalableInertialMatrix(totalDiagElementParts * (1.0 / 60.0), totalOffDiag * (1.0 / 60.0));
}

ScalableInertialMatrix Polyhedron::getScalableInertiaAroundCenterOfMass() const {
	return getScalableInertia(CFrame(getCenterOfMass()));
}

/*
	The total inertial matrix is given by the integral over the volume of the shape of the following matrix:
	[[
	[y^2+z^2,    xy,    xz],
	[xy,    x^2+z^2,    yz],
	[xz,    yz,    x^2+y^2]
	]]

	This has been reworked to a surface integral resulting in the given formulae
*/
SymmetricMat3 Polyhedron::getInertia(const CFrame& reference) const {
	return getScalableInertia(reference).toMatrix();
}

SymmetricMat3 Polyhedron::getInertiaAroundCenterOfMass() const {
	return getScalableInertiaAroundCenterOfMass().toMatrix();
}
};
