#pragma once

struct Simplex;

struct MinkowskiPointIndices {
	int indices[2];

	int& operator[](int i) { return indices[i]; }
	bool operator==(const MinkowskiPointIndices& other) const {
		return this->indices[0] == other.indices[0] &&
			this->indices[1] == other.indices[1];
	}
};

#include "../math/vec3.h"
#include "convexShapeBuilder.h"
#include "computationBuffer.h"
#include "../math/utils.h"
#include "../../util/log.h"
#include "../debug.h"

struct Simplex {
	Vec3 A, B, C, D;
	MinkowskiPointIndices At, Bt, Ct, Dt;
	int order;
	Simplex(Vec3 A, MinkowskiPointIndices At) : A(A), At(At), order(1) {}
	Simplex(Vec3 A, Vec3 B, MinkowskiPointIndices At, MinkowskiPointIndices Bt) : A(A), B(B), At(At), Bt(Bt), order(2) {}
	Simplex(Vec3 A, Vec3 B, Vec3 C, MinkowskiPointIndices At, MinkowskiPointIndices Bt, MinkowskiPointIndices Ct) : A(A), B(B), C(C), At(At), Bt(Bt), Ct(Ct), order(3) {}
	Simplex(Vec3 A, Vec3 B, Vec3 C, Vec3 D, MinkowskiPointIndices At, MinkowskiPointIndices Bt, MinkowskiPointIndices Ct, MinkowskiPointIndices Dt) : A(A), B(B), C(C), D(D), At(At), Bt(Bt), Ct(Ct), Dt(Dt), order(4) {}
	void insert(Vec3 newA, MinkowskiPointIndices newAt) {
		D = C;
		C = B;
		B = A;
		A = newA;
		Dt = Ct;
		Ct = Bt;
		Bt = At;
		At = newAt;
		order++;
	}
};

Simplex runGJK(const Shape& first, const Shape& second, Vec3 initialSearchDirection);
bool runEPA(const Shape& first, const Shape& second, Simplex s, Vec3& intersection, Vec3& exitVector, ComputationBuffers& bufs);
