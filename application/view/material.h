#pragma once

#include "../engine/math/vec3.h"

#include "texture.h"

struct Material {
	Texture* texture = nullptr;
	Texture* normal = nullptr;

	Vec3f ambient;
	Vec3f diffuse;
	Vec3f specular;
	float reflectance;

	Material(Vec3f ambient, Vec3f diffuse, Vec3f specular, float reflectance, Texture* texture, Texture* normalMap) : ambient(ambient), diffuse(diffuse), specular(specular), reflectance(reflectance), texture(texture), normal(normalMap) {};
	Material(Vec3f ambient, Vec3f diffuse, Vec3f specular, float reflectance, Texture* texture) : ambient(ambient), diffuse(diffuse), specular(specular), reflectance(reflectance), texture(texture) {};
	Material(Vec3f ambient, Vec3f diffuse, Vec3f specular, float reflectance) : ambient(ambient), diffuse(diffuse), specular(specular), reflectance(reflectance) {};
	Material(Vec3f ambient) : Material(ambient, Vec3f(1.0f), Vec3f(1.0f), 1.0f, texture) {};
	Material(Texture* texture) : Material(Vec3f(1.0), Vec3f(1.0), Vec3f(1.0f), 1.0f, texture) {};
	Material() : Material(Vec3f(0.8f), Vec3f(1.0f), Vec3f(1.0f), 1.0f) {};

	void setTexture(Texture* texture) {
		texture->unit = 0;
		this->texture = texture;
	}

	void setNormalMap(Texture* normalMap) {
		normalMap->unit = 1;
		this->normal = normalMap;
	}
};