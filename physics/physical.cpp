#include "physical.h"

#include "../util/log.h"

#include "inertia.h"
#include "world.h"
#include "math/linalg/mat.h"
#include "math/linalg/misc.h"
#include "math/linalg/trigonometry.h"

#include "debug.h"
#include <algorithm>
#include <limits>

#include "integrityCheck.h"

#include "misc/validityHelper.h"


/*
	===== Physical Structure =====
*/

#pragma region structure

Physical::Physical(Part* part, MotorizedPhysical* mainPhysical) : rigidBody(part), mainPhysical(mainPhysical) {
	if (part->parent != nullptr) {
		throw "This part is already in another physical!";
	}
	part->parent = this;
}

Physical::Physical(Physical&& other) noexcept :
	rigidBody(std::move(other.rigidBody)), 
	mainPhysical(other.mainPhysical) {
	for(Part& p : this->rigidBody) {
		p.parent = this;
	}
	for(ConnectedPhysical& p : this->childPhysicals) {
		p.parent = this;
	}
	other.mainPhysical = nullptr;
}

Physical& Physical::operator=(Physical&& other) noexcept {
	this->rigidBody = std::move(other.rigidBody);
	for(Part& p : this->rigidBody) {
		p.parent = this;
	}

	this->mainPhysical = other.mainPhysical;
	
	return *this;
}

void Physical::makeMainPart(Part* newMainPart) {
	if (rigidBody.getMainPart() == newMainPart) {
		Log::warn("Attempted to replace mainPart with mainPart");
		return;
	}
	AttachedPart& atPart = rigidBody.getAttachFor(newMainPart);
	
	makeMainPart(atPart);
}

void Physical::makeMainPart(AttachedPart& newMainPart) {
	CFrame newCenterCFrame = rigidBody.makeMainPart(newMainPart);

	// Update attached physicals
	for(ConnectedPhysical& connectedPhys : childPhysicals) {
		connectedPhys.attachOnParent = newCenterCFrame.globalToLocal(connectedPhys.attachOnParent);
	}
	if(!isMainPhysical()) {
		ConnectedPhysical* self = (ConnectedPhysical*) this;
		self->attachOnThis = newCenterCFrame.globalToLocal(self->attachOnThis);
	}
}

static inline bool liesInVector(const std::vector<AttachedPart>& vec, const AttachedPart* ptr) {
	return vec.begin()._Ptr <= ptr && vec.end()._Ptr > ptr;
}

void ConnectedPhysical::makeMainPhysical() {
	throw "Not implemented!";
}

void Physical::attachPhysical(Physical&& phys, const CFrame& attachment) {
	if(phys.mainPhysical->world != nullptr) {
		phys.mainPhysical->world->removeMainPhysical(phys.mainPhysical);
	}
	if(this->mainPhysical->world != nullptr) {
		Part* main = this->rigidBody.getMainPart();
		NodeStack stack = this->mainPhysical->world->objectTree.findGroupFor(main, main->getStrictBounds());
		TreeNode& group = **stack;
		for(Part& p : phys.rigidBody) {
			this->mainPhysical->world->objectTree.addToExistingGroup(&p, p.getStrictBounds(), group);
		}
		stack.expandBoundsAllTheWayToTop();
	}

	for(Part& part : phys.rigidBody) {
		part.parent = this;
	}

	this->rigidBody.attach(std::move(phys.rigidBody), attachment);
}

bool Physical::isMainPhysical() const {
	Physical* ptr = mainPhysical;
	return ptr == this;
}

void Physical::removeChild(ConnectedPhysical* child) {
	assert(child >= this->childPhysicals.begin()._Ptr && child < this->childPhysicals.end()._Ptr);

	*child = std::move(childPhysicals.back());
	childPhysicals.pop_back();
}

void Physical::attachPhysical(Physical&& phys, HardConstraint* constraint, const CFrame& attachToThis, const CFrame& attachToThat) {
	throw "todo makeMainPhysical";
	if(phys.isMainPhysical()) {
		ConnectedPhysical childToAdd(std::move(phys), this, constraint, attachToThat, attachToThis);
		childPhysicals.push_back(std::move(childToAdd));
		delete &phys;
	} else {
		ConnectedPhysical& connectedPhys = static_cast<ConnectedPhysical&>(phys);
		Physical* parent = connectedPhys.parent;
		assert(parent != nullptr);
		ConnectedPhysical childToAdd(std::move(phys), this, constraint, attachToThat, attachToThis);
		childPhysicals.push_back(std::move(childToAdd));
		parent->removeChild(&connectedPhys);
	}

	

	childPhysicals.back().updateCFrame(this->getCFrame());

	mainPhysical->refreshPhysicalProperties();
}

void Physical::attachPart(Part* part, HardConstraint* constraint, const CFrame& attachToThis, const CFrame& attachToThat) {
	if(part->parent == nullptr) {
		childPhysicals.push_back(ConnectedPhysical(Physical(part, this->mainPhysical), this, constraint, attachToThat, attachToThis));
	} else {
		attachPhysical(std::move(*part->parent), constraint, attachToThis, part->transformCFrameToParent(attachToThat));
	}

	childPhysicals.back().updateCFrame(this->getCFrame());

	mainPhysical->refreshPhysicalProperties();
}


static Physical* findPhysicalParent(Physical* findIn, const ConnectedPhysical* toBeFound) {
	if(toBeFound >= findIn->childPhysicals.begin()._Ptr && toBeFound < findIn->childPhysicals.end()._Ptr) {
		return findIn;
	}
	for(ConnectedPhysical& p : findIn->childPhysicals) {
		Physical* result = findPhysicalParent(&p, toBeFound);
		if(result != nullptr) {
			return result;
		}
	}
	return nullptr;
}

void Physical::attachPart(Part* part, const CFrame& attachment) {
	if (part->parent != nullptr) { // part is already in a physical
		if (part->parent == this) {
			throw "Part already attached to this physical!";
		} else {
			// attach other part's entire physical
			if (part->isMainPart()) {
				attachPhysical(std::move(*part->parent), attachment);
			} else {
				CFrame newAttach = attachment.localToGlobal(~part->parent->rigidBody.getAttachFor(part).attachment);
				attachPhysical(std::move(*part->parent), newAttach);
			}
		}
	} else {
		if(this->mainPhysical->world != nullptr) {
			this->mainPhysical->world->mergePartAndPhysical(this->mainPhysical, part);
		}
		part->parent = this;
		rigidBody.attach(part, attachment);
	}
	this->mainPhysical->refreshPhysicalProperties();
}

void Physical::detachAllChildPhysicals() {
	WorldPrototype* world = this->mainPhysical->world;
	for(ConnectedPhysical& child : childPhysicals) {
		delete child.constraintWithParent;
		MotorizedPhysical* newPhys = new MotorizedPhysical(std::move(static_cast<Physical&>(child)));
		
		if(world != nullptr) {
			world->splitPhysical(this->mainPhysical, newPhys);
		}
	}

	childPhysicals.clear();
}


void Physical::detachChild(ConnectedPhysical&& formerChild) {
	delete formerChild.constraintWithParent;
	MotorizedPhysical* newPhys = new MotorizedPhysical(std::move(static_cast<Physical&>(formerChild)));
	WorldPrototype* world = this->mainPhysical->world;
	if(world != nullptr) {
		world->splitPhysical(this->mainPhysical, newPhys);
	}
	childPhysicals.remove(std::move(formerChild));
}

void Physical::detachPart(Part* part, bool partStaysInWorld) {
	if(part == rigidBody.getMainPart()) {
		if(rigidBody.getPartCount() == 1) {
			this->detachAllChildPhysicals();
			MotorizedPhysical* mainPhys = this->mainPhysical; // save main physical because it'll get deleted by parent->detachChild()
			if(!this->isMainPhysical()) {
				ConnectedPhysical& self = static_cast<ConnectedPhysical&>(*this);
				Physical* parent = self.parent;
				parent->detachChild(std::move(self));
			}
			mainPhys->refreshPhysicalProperties();
			// new parent
			assert(part->parent->childPhysicals.size() == 0);
			if(!partStaysInWorld) {
				delete part->parent->mainPhysical;
			}
		} else {
			AttachedPart& newMainPartAndLaterOldPart = rigidBody.parts.back();
			makeMainPart(newMainPartAndLaterOldPart); // now points to old part
			rigidBody.detach(std::move(newMainPartAndLaterOldPart));

			this->mainPhysical->refreshPhysicalProperties();
		}
	} else {
		rigidBody.detach(part);

		this->mainPhysical->refreshPhysicalProperties();
	}

	if(partStaysInWorld) {
		new MotorizedPhysical(part);
	}
}

void Physical::notifyPartPropertiesChanged(Part* part) {
	rigidBody.refreshWithNewParts();
}
void Physical::notifyPartPropertiesAndBoundsChanged(Part* part, const Bounds& oldBounds) {
	notifyPartPropertiesChanged(part);
	if(this->mainPhysical->world != nullptr) {
		this->mainPhysical->world->updatePartBounds(part, oldBounds);
	}
}

void Physical::updateAttachedPhysicals(double deltaT) {
	GlobalCFrame thisCFrame = getCFrame();
	for(ConnectedPhysical& p : childPhysicals) {
		p.constraintWithParent->update(deltaT);
		p.updateCFrame(thisCFrame);
		p.updateAttachedPhysicals(deltaT);
	}
}

void Physical::setCFrame(const GlobalCFrame& newCFrame) {
	if(this->mainPhysical->world != nullptr) {
		Bounds oldMainPartBounds = this->rigidBody.mainPart->getStrictBounds();

		rigidBody.setCFrame(newCFrame);

		this->mainPhysical->world->updatePartGroupBounds(this->rigidBody.mainPart, oldMainPartBounds);
	} else {
		rigidBody.setCFrame(newCFrame);
	}
}

void Physical::setPartCFrame(Part* part, const GlobalCFrame& newCFrame) {
	if(part == rigidBody.mainPart) {
		setCFrame(newCFrame);
	} else {
		CFrame attach = rigidBody.getAttachFor(part).attachment;
		GlobalCFrame newMainCFrame = newCFrame.localToGlobal(~attach);

		setCFrame(newMainCFrame);
	}
}

#pragma endregion

/*
	===== Refresh functions =====
*/

#pragma region refresh

void MotorizedPhysical::rotateAroundCenterOfMassUnsafe(const RotMat3& rotation) {
	rigidBody.rotateAroundLocalPoint(totalCenterOfMass, rotation);
}
void Physical::translateUnsafeRecursive(const Vec3Fix& translation) {
	rigidBody.translate(translation);
	for(ConnectedPhysical& conPhys : childPhysicals) {
		conPhys.translateUnsafeRecursive(translation);
	}
}
void MotorizedPhysical::rotateAroundCenterOfMass(const RotMat3& rotation) {
	Bounds oldBounds = this->rigidBody.mainPart->getStrictBounds();
	rotateAroundCenterOfMassUnsafe(rotation);
	mainPhysical->world->updatePartGroupBounds(this->rigidBody.mainPart, oldBounds);
}
void MotorizedPhysical::translate(const Vec3& translation) {
	Bounds oldBounds = this->rigidBody.mainPart->getStrictBounds();
	translateUnsafeRecursive(translation);
	mainPhysical->world->updatePartGroupBounds(this->rigidBody.mainPart, oldBounds);
}

static std::pair<Vec3, double> getRecursiveCenterOfMass(const Physical& phys) {
	Vec3 totalCOM = phys.rigidBody.mass * phys.rigidBody.localCenterOfMass;
	double totalMass = phys.rigidBody.mass;
	for(const ConnectedPhysical& conPhys : phys.childPhysicals) {
		CFrame relFrame = conPhys.getRelativeCFrameToParent();
		std::pair<Vec3, double> localCOM = getRecursiveCenterOfMass(conPhys);
		totalCOM += localCOM.second * relFrame.localToGlobal(localCOM.first);
		totalMass += localCOM.second;
	}
	return std::pair<Vec3, double>(totalCOM / totalMass, totalMass);
}

static SymmetricMat3 getRecursiveInertia(const Physical& phys, const CFrame& offsetCFrame, const Vec3& localCOMOfMain) {
	SymmetricMat3 totalInertia = getTransformedInertiaAroundCenterOfMass(phys.rigidBody.inertia, phys.rigidBody.localCenterOfMass, offsetCFrame, localCOMOfMain, phys.rigidBody.mass);

	for(const ConnectedPhysical& conPhys : phys.childPhysicals) {
		CFrame cframeToConPhys = conPhys.getRelativeCFrameToParent();

		CFrame offsetOfConPhys = offsetCFrame.localToGlobal(cframeToConPhys);

		totalInertia += getRecursiveInertia(conPhys, offsetOfConPhys, localCOMOfMain);
	}
	return totalInertia;
}

void MotorizedPhysical::refreshPhysicalProperties() {
	std::pair<Vec3, double> result = getRecursiveCenterOfMass(*this);
	totalCenterOfMass = result.first;
	totalMass = result.second;

	SymmetricMat3 totalInertia = getRecursiveInertia(*this, CFrame(0, 0, 0), totalCenterOfMass);

	forceResponse = SymmetricMat3::IDENTITY() * (1 / totalMass);
	momentResponse = ~totalInertia;
}

#pragma endregion

/*
	===== Update =====
*/

#pragma region update

void MotorizedPhysical::update(double deltaT) {
	refreshPhysicalProperties();

	Vec3 accel = forceResponse * totalForce * deltaT;
	
	Vec3 localMoment = getCFrame().relativeToLocal(totalMoment);
	Vec3 localRotAcc = momentResponse * localMoment * deltaT;
	Vec3 rotAcc = getCFrame().localToRelative(localRotAcc);

	totalForce = Vec3();
	totalMoment = Vec3();

	motionOfCenterOfMass.velocity += accel;
	motionOfCenterOfMass.angularVelocity += rotAcc;

	Vec3 movement = motionOfCenterOfMass.velocity * deltaT + accel * deltaT * deltaT / 2;

	Mat3 rotation = fromRotationVec(motionOfCenterOfMass.angularVelocity * deltaT);

	rotateAroundCenterOfMassUnsafe(rotation);
	translateUnsafeRecursive(movement);

	updateAttachedPhysicals(deltaT);
}

void ConnectedPhysical::updateCFrame(const GlobalCFrame& parentCFrame) {
	GlobalCFrame newPosition = parentCFrame.localToGlobal(getRelativeCFrameToParent());
	rigidBody.setCFrame(newPosition);
}

#pragma endregion

/*
	===== application of forces and the like
*/

#pragma region apply

void MotorizedPhysical::applyForceAtCenterOfMass(Vec3 force) {
	CHECK_VALID_VEC(force);
	totalForce += force;

	Debug::logVector(getCenterOfMass(), force, Debug::FORCE);
}

void MotorizedPhysical::applyForce(Vec3Relative origin, Vec3 force) {
	CHECK_VALID_VEC(origin);
	CHECK_VALID_VEC(force);
	totalForce += force;

	Debug::logVector(getCenterOfMass() + origin, force, Debug::FORCE);

	applyMoment(origin % force);
}

void MotorizedPhysical::applyMoment(Vec3 moment) {
	CHECK_VALID_VEC(moment);
	totalMoment += moment;
	Debug::logVector(getCenterOfMass(), moment, Debug::MOMENT);
}

void MotorizedPhysical::applyImpulseAtCenterOfMass(Vec3 impulse) {
	CHECK_VALID_VEC(impulse);
	Debug::logVector(getCenterOfMass(), impulse, Debug::IMPULSE);
	motionOfCenterOfMass.velocity += forceResponse * impulse;
}
void MotorizedPhysical::applyImpulse(Vec3Relative origin, Vec3Relative impulse) {
	CHECK_VALID_VEC(origin);
	CHECK_VALID_VEC(impulse);
	Debug::logVector(getCenterOfMass() + origin, impulse, Debug::IMPULSE);
	motionOfCenterOfMass.velocity += forceResponse * impulse;
	Vec3 angularImpulse = origin % impulse;
	applyAngularImpulse(angularImpulse);
}
void MotorizedPhysical::applyAngularImpulse(Vec3 angularImpulse) {
	CHECK_VALID_VEC(angularImpulse);
	Debug::logVector(getCenterOfMass(), angularImpulse, Debug::ANGULAR_IMPULSE);
	Vec3 localAngularImpulse = getCFrame().relativeToLocal(angularImpulse);
	Vec3 localRotAcc = momentResponse * localAngularImpulse;
	Vec3 rotAcc = getCFrame().localToRelative(localRotAcc);
	motionOfCenterOfMass.angularVelocity += rotAcc;
}

void MotorizedPhysical::applyDragAtCenterOfMass(Vec3 drag) {
	CHECK_VALID_VEC(drag);
	Debug::logVector(getCenterOfMass(), drag, Debug::POSITION);
	translate(forceResponse * drag);
}
void MotorizedPhysical::applyDrag(Vec3Relative origin, Vec3Relative drag) {
	CHECK_VALID_VEC(origin);
	CHECK_VALID_VEC(drag);
	Debug::logVector(getCenterOfMass() + origin, drag, Debug::POSITION);
	translateUnsafeRecursive(forceResponse * drag);
	Vec3 angularDrag = origin % drag;
	applyAngularDrag(angularDrag);
}
void MotorizedPhysical::applyAngularDrag(Vec3 angularDrag) {
	CHECK_VALID_VEC(angularDrag);
	Debug::logVector(getCenterOfMass(), angularDrag, Debug::INFO_VEC);
	Vec3 localAngularDrag = getCFrame().relativeToLocal(angularDrag);
	Vec3 localRotAcc = momentResponse * localAngularDrag;
	Vec3 rotAcc = getCFrame().localToRelative(localRotAcc);
	rotateAroundCenterOfMassUnsafe(fromRotationVec(rotAcc));
}


void Physical::applyDragToPhysical(Vec3 origin, Vec3 drag) {
	Vec3 COMOffset = mainPhysical->getCenterOfMass() - getCFrame().getPosition();
	mainPhysical->applyDrag(origin + COMOffset, drag);
}
void Physical::applyImpulseToPhysical(Vec3 origin, Vec3 impulse) {
	Vec3 COMOffset = mainPhysical->getCenterOfMass() - getCFrame().getPosition();
	mainPhysical->applyImpulse(origin + COMOffset, impulse);
}
void Physical::applyForceToPhysical(Vec3 origin, Vec3 force) {
	Vec3 COMOffset = mainPhysical->getCenterOfMass() - getCFrame().getPosition();
	mainPhysical->applyForce(origin + COMOffset, force);
}

#pragma endregion

#pragma region getters

double Physical::getVelocityKineticEnergy() const {
	return rigidBody.mass * lengthSquared(getMotion().velocity) / 2;
}
double Physical::getAngularKineticEnergy() const {
	Vec3 localAngularVel = getCFrame().relativeToLocal(getMotion().angularVelocity);
	return (rigidBody.inertia * localAngularVel) * localAngularVel / 2;
}
double Physical::getKineticEnergy() const {
	return getVelocityKineticEnergy() + getAngularKineticEnergy();
}
CFrame Physical::getRelativeCFrameToMain() const {
	return mainPhysical->getCFrame().globalToLocal(this->getCFrame());
}
Vec3 Physical::localToMain(const Vec3Local& vec) const {
	Position globalPos = this->getCFrame().localToGlobal(vec);
	return mainPhysical->getCFrame().globalToLocal(globalPos);
}

/*
	Computes the force->acceleration transformation matrix
	Such that:
	a = M*F
*/
SymmetricMat3 MotorizedPhysical::getResponseMatrix(const Vec3Local& r) const {
	Mat3 crossMat = createCrossProductEquivalent(r);

	SymmetricMat3 rotationFactor = multiplyLeftRight(momentResponse , crossMat);

	return forceResponse + rotationFactor;
}

Mat3 MotorizedPhysical::getResponseMatrix(const Vec3Local& actionPoint, const Vec3Local& responsePoint) const {
	Mat3 actionCross = createCrossProductEquivalent(actionPoint);
	Mat3 responseCross = createCrossProductEquivalent(responsePoint);

	Mat3 rotationFactor = responseCross * momentResponse * actionCross;

	return Mat3(forceResponse) - rotationFactor;
}
double MotorizedPhysical::getInertiaOfPointInDirectionLocal(const Vec3Local& localPoint, const Vec3Local& localDirection) const {
	SymmetricMat3 accMat = getResponseMatrix(localPoint);

	Vec3 force = localDirection;
	Vec3 accel = accMat * force;
	double accelInForceDir = accel * localDirection / lengthSquared(localDirection);

	return 1 / accelInForceDir;

	/*SymmetricMat3 accelToForceMat = ~accMat;
	Vec3 imaginaryForceForAcceleration = accelToForceMat * direction;
	double forcePerAccelRatio = imaginaryForceForAcceleration * direction / direction.lengthSquared();
	return forcePerAccelRatio;*/
}

double MotorizedPhysical::getInertiaOfPointInDirectionRelative(const Vec3Relative& relPoint, const Vec3Relative& relDirection) const {
	return getInertiaOfPointInDirectionLocal(getCFrame().relativeToLocal(relPoint), getCFrame().relativeToLocal(relDirection));
}

CFrame ConnectedPhysical::getRelativeCFrameToParent() const {
	return attachOnParent.localToGlobal(constraintWithParent->getRelativeCFrame().localToGlobal(~attachOnThis));
}

Position MotorizedPhysical::getCenterOfMass() const {
	return getCFrame().localToGlobal(totalCenterOfMass);
}

GlobalCFrame MotorizedPhysical::getCenterOfMassCFrame() const {
	return GlobalCFrame(getCFrame().localToGlobal(totalCenterOfMass), getCFrame().getRotation());
}

Motion Physical::getMotion() const {
	if(this->isMainPhysical()) {
		MotorizedPhysical* self = (MotorizedPhysical*) this;

		return self->motionOfCenterOfMass.getMotionOfPoint(getCFrame().localToRelative(rigidBody.localCenterOfMass - self->totalCenterOfMass));
	} else {
		ConnectedPhysical* self = (ConnectedPhysical*) this;
		
		Physical* parent = self->parent;

		// All motion and offset variables here are expressed in the global frame

		Motion parentMotion = parent->getMotion();

		Vec3 connectionOffsetOnParent = parent->getCFrame().localToRelative(self->attachOnParent.getPosition() - parent->rigidBody.localCenterOfMass);

		Motion motionOfConnectOnParent = parentMotion.getMotionOfPoint(connectionOffsetOnParent);

		Vec3 jointOffset = parent->getCFrame().localToRelative(self->attachOnParent.localToRelative(self->constraintWithParent->getRelativeCFrame().getPosition()));

		Motion motionPastJoint = motionOfConnectOnParent.addRelativeMotion(self->constraintWithParent->getRelativeMotion()).getMotionOfPoint(jointOffset);

		Vec3 connectionOffsetOnSelf = self->getCFrame().localToRelative(self->attachOnThis.getPosition() - rigidBody.localCenterOfMass);

		return motionPastJoint.getMotionOfPoint(-connectionOffsetOnSelf);
	}
}


size_t Physical::getNumberOfPartsInThisAndChildren() const {
	size_t totalParts = rigidBody.getPartCount();
	for(const ConnectedPhysical& child : childPhysicals) {
		totalParts += child.getNumberOfPartsInThisAndChildren();
	}
	return totalParts;
}

void Physical::setMainPhysicalRecursive(MotorizedPhysical* newMainPhysical) {
	this->mainPhysical = newMainPhysical;
	for(ConnectedPhysical& child : childPhysicals) {
		child.setMainPhysicalRecursive(newMainPhysical);
	}
}

#pragma endregion


ConnectedPhysical::ConnectedPhysical(Physical&& phys, Physical* parent, HardConstraint* constraintWithParent, const CFrame& attachOnThis, const CFrame& attachOnParent) :
	Physical(std::move(phys)), parent(parent), attachOnThis(attachOnThis), attachOnParent(attachOnParent), constraintWithParent(constraintWithParent) {
}

MotorizedPhysical::MotorizedPhysical(Part* mainPart) : Physical(mainPart, this) {
	refreshPhysicalProperties();
}

MotorizedPhysical::MotorizedPhysical(Physical&& movedPhys) : Physical(std::move(movedPhys)) {
	this->setMainPhysicalRecursive(this);
	refreshPhysicalProperties();
}

void MotorizedPhysical::ensureWorld(WorldPrototype* world) {
	if(this->world == world) return;
	if(this->world != nullptr) {
		this->world->removeMainPhysical(this);
	}
	throw "TODO";
}

#pragma region isValid

bool Physical::isValid() const {
	assert(rigidBody.isValid());

	for(const ConnectedPhysical& p : childPhysicals) {
		assert(p.isValid());
	}

	return true;
}
bool MotorizedPhysical::isValid() const {
	assert(Physical::isValid());
	
	assert(isVecValid(totalForce));
	assert(isVecValid(totalMoment));
	assert(isfinite(totalMass));
	assert(isVecValid(totalCenterOfMass));
	assert(isMatValid(forceResponse));
	assert(isMatValid(momentResponse));

	return true;
}

bool ConnectedPhysical::isValid() const {
	assert(Physical::isValid());

	assert(isCFrameValid(attachOnParent));
	assert(isCFrameValid(attachOnThis));

	return true;
}

#pragma endregion
