#include "stdafx.h"
#include "IsohypseBatchingMarker.h"
#include "IsohypseData.h"
#include "Scene/Generation/OmnigenGenerationData.h"

QSharedPointer<Isohypse> spawnBatchedIH(const IHProtoData& data, int inLevel)
{
	auto geometry = buildLineGeometry(data.pts, true);
	for (auto&& p : geometry.vertices)
		p.setY(data.height);

	TODO("Set y=60 #if DEBUG_2D_VIEW");

	static IsohypseBatchParams sParams;
	auto section = spawnBatched(std::move(geometry), sParams);

	section->level = inLevel;
	section->descendants.resize(data.pts.size());
	section->preflow.resize(data.pts.size());
	section->parentIhs = data.parentIhs;
	section->data = data;
	emit Editable::created(section);

	return section;
}

const QVector3D& Isohypse::getSourcePoint(int i) const
{
	static QVector3D dummy(-1, -1, -1);
	if (level == 0)
		return dummy;

	return data.sources[i].getPoint();
}

IHSrcInfo Isohypse::getNearestDescendant(int i, int dir /* = 0 */, const QVector3D& ignoredPoint /* = QVector3D(-1, -1, -1) */)
{
	auto&& cPoints = getCircularPoints();

	if (descendants[i] && descendants[i].ih->getLevel() >= 0)
		return descendants[i];

	for (int s = 1; s <= cPoints.getSize() / 2; ++s)
	{
		if (dir < 1)
			if (int ai = cPoints.findIdx(i, -s); descendants[ai] && descendants[ai].ih->getLevel() >= 0)
				if (!vEq(descendants[ai].getPoint(), ignoredPoint))
					return descendants[ai];

		if (dir > -1)
			if (int ai = cPoints.findIdx(i, s); descendants[ai] && descendants[ai].ih->getLevel() >= 0)
				if (!vEq(descendants[ai].getPoint(), ignoredPoint))
					return descendants[ai];
	}
 
 	return IHSrcInfo();
}

void Isohypse::setPoints(const std::vector<QVector3D>& newPoints)
{
	emit Editable::aboutToBeModified(sharedFromThis());
	setGeometry(buildLineGeometry(newPoints, true));
	emit Editable::modified(sharedFromThis());
}

void Isohypse::setDescendant(int i, const IHSrcInfo& descendant)
{
	emit Editable::aboutToBeModified(sharedFromThis());
	descendants[i] = descendant;
	emit Editable::modified(sharedFromThis());
}

void Isohypse::addPreflow(int i, const IHSrcInfo& preflowPoint)
{
	emit Editable::aboutToBeModified(sharedFromThis());

	for (auto&& p : preflow[i])
		if (p.ih == preflowPoint.ih)
		{
			p.indices << preflowPoint.idx;
			break;
		}

	IHSrcInfoMulti newInfo;
	newInfo.ih = preflowPoint.ih;
	newInfo.indices << preflowPoint.idx;
	preflow[i] << newInfo;

	emit Editable::modified(sharedFromThis());
}

void Isohypse::addParent(BatchedSection* parent)
{
	Q_ASSERT(parent);
	emit Editable::aboutToBeModified(sharedFromThis());
	parentIhs.insert(parent);
	emit Editable::modified(sharedFromThis());
}

float Isohypse::getLength() const
{
	if (std::scoped_lock lock(lengthGuard); lazyLength == -1.f)
	{
		auto&& pts = getCircularPoints();
		lazyLength = 0.0f;

		for (int i = 0; i < pts.getSize(); ++i)
			lazyLength += distance(pts[i], pts[pts.findIdx(i, 1)]);

		Q_ASSERT(lazyLength > 0.0f);
	}

	return lazyLength;
}