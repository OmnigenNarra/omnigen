#pragma once
#include "Scene/OmnigenDrawable.h"
#include "Data/DomainData.h"

class DDomainPaintingPreview : public OmnigenDrawable
{
public:
	DDomainPaintingPreview();

	void update(const GPoint& newSquare);
	const auto& getSquares() const { return squares; }

	virtual ERenderPriority getRenderPriority() const override;
	virtual quint32 getShaderLabel() const override { return typeid(decltype(*this)).hash_code(); };
	virtual void bindShader(const OmnigenCamera& camera) override;
	virtual void draw() override;
	virtual void unbindShader() override;
	virtual bool shouldDraw(int vIdx) const override;
	virtual ShaderPipeline& getShaderPipeline() const override { return shaderPipeline; };

protected:
	virtual void createDefaultLodLevel() override;
	virtual void cacheBoundingBox() override;
	virtual void createShader() override;

private:
	QSet<GPoint> squares;

	static inline ShaderPipeline shaderPipeline;
};

