#pragma once

#include "../engine/layer/layer.h"

class Screen;

class ModelLayer : public Layer {
private:
	unsigned int id;
	float texture[28];
public:
	ModelLayer();
	ModelLayer(Screen* screen, char flags = None);

	void onInit() override;
	void onUpdate() override;
	void onEvent(Event& event) override;
	void onRender() override;
	void onClose() override;
};