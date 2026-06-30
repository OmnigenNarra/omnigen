#include "stdafx.h"
#include "IHSrcInfo.h"
#include "IsohypseBatchingMarker.h"

bool IHSrcInfo::operator==(const IHSrcInfo& other) const noexcept
{
	return operator<=>(other) == std::strong_ordering::equal;
}

const QVector3D& IHSrcInfo::getPoint(int adj) const
{
	if (idx == -1)
	{
		static QVector3D dummy(-1, -1, -1);
		return dummy;
	}

	auto&& pts = ih->getCircularPoints();
	int i = pts.findIdx(idx, adj);
	return pts[i];
}

const IHSrcInfo& IHSrcInfo::getSource() const
{
	auto&& sources = ih->getSources();
	if ((idx == -1) || (idx >= sources.size()))
	{
		static IHSrcInfo dummy;
		return dummy;
	}

	return sources[idx];
}

const IHSrcInfo& IHSrcInfo::getDescendant() const
{
	if (idx == -1)
	{
		static IHSrcInfo dummy;
		return dummy;
	}

	auto&& descendants = ih->getDescendants();
	return descendants[idx];
}

IHSrcInfoMulti::IHSrcInfoMulti(const IHSrcInfo& single)
	: ih(single.ih)
	, indices({ single.idx })
{
}

IHSrcInfoMulti::IHSrcInfoMulti(Isohypse* inIh, const std::vector<int>& idxs)
	: ih(inIh)
	, indices(idxs)
{
}

IHSrcInfo IHSrcInfoMulti::first() const
{
	return { ih, indices.front() };
}

IHSrcInfo IHSrcInfoMulti::last() const
{
	return { ih, indices.back() };
}

bool IHSrcInfoMulti::contains(const IHSrcInfo& src) const
{
	return (ih == src.ih) && (::contains(indices, src.idx));
}

void omniSave(const IHSrcInfo& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << (object.ih ? object.ih->getGuid() : qint64(-1));
	omniBin << object.idx;
}

void omniSave(const IHSrcInfoMulti& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << (object.ih ? object.ih->getGuid() : qint64(-1));
	omniBin << object.indices;
}

void omniLoad(IHSrcInfo& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.ihGuid;
	omniBin >> object.idx;
}

void omniLoad(IHSrcInfoMulti& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.ihGuid;
	omniBin >> object.indices;
}