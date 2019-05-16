#include "camera.h"

#include "GL\glew.h"
#include "GLFW\glfw3.h"

#include "picker.h"
#include "screen.h"

void Camera::setPosition(Vec3 position) {
	cframe.position = position;
}

void Camera::setPosition(double x, double y, double z) {
	setPosition(Vec3(x, y, z));
}

void Camera::setRotation(double alpha, double beta, double gamma) {
	Mat3 euler = fromEulerAngles(alpha, beta, gamma);
	cframe.rotation = euler;
}

void Camera::setRotation(Vec3 rotation) {
	setRotation(rotation.x, rotation.y, rotation.z);
}

void Camera::rotate(Screen& screen, double dalpha, double dbeta, double dgamma, bool leftDragging) {
	cframe.rotation = rotX(rspeed * dalpha) * cframe.rotation * rotY(rspeed * dbeta);

	if (leftDragging) moveGrabbedPhysicalLateral(screen);
}

void Camera::rotate(Screen& screen, Vec3 delta, bool leftDragging) {
	rotate(screen, delta.x, delta.y, delta.z, leftDragging);
}

void Camera::move(Screen& screen, double dx, double dy, double dz, bool leftDragging) {
	(*screen.eventHandler.cameraMoveHandler) (screen, this, Vec3(dx, dy, dz));
	
	Vec3 translation = Vec3();

	if (dx != 0) {
		Vec3 cameraRotationX = cframe.rotation.transpose() * Vec3(1, 0, 0);
		Vec3 translationX = Vec3(cameraRotationX.x, 0, cameraRotationX.z).normalize() * dx;
		translation += translationX;

		if (leftDragging) moveGrabbedPhysicalLateral(screen);
	}

	if (dy != 0) {
		Vec3 translationY = Vec3(0, dy, 0);
		translation += translationY;

		if (leftDragging) moveGrabbedPhysicalLateral(screen);
	}

	if (dz != 0) {
		Vec3 cameraRotationZ = cframe.rotation.transpose() * Vec3(0, 0, 1);
		Vec3 translationZ = Vec3(cameraRotationZ.x, 0, cameraRotationZ.z).normalize() * dz;
		translation += translationZ;

		if (leftDragging) moveGrabbedPhysicalTransversal(screen, -speed * dz);
	}

	translation *= speed;

	cframe.translate(translation);
}

void Camera::move(Screen& screen, Vec3 delta, bool leftDragging) {
	move(screen, delta.x, delta.y, delta.z, leftDragging);
}

void Camera::update() {
	if(attachment != nullptr) {
		cframe.position = attachment->cframe.position;
	}
}