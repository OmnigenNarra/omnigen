#pragma once
#include "Scene/Generation/Common/Markers/BatchMarker.h"

struct IsohypseBatchParams;
template<> struct BatchedSection<IsohypseBatchParams>;
using Isohypse = BatchedSection<IsohypseBatchParams>;

struct IHSrcInfo
{
	Isohypse* ih = nullptr;
	int idx = -1;
	qint64 ihGuid = -1;

	const QVector3D& getPoint(int adj = 0) const;
	const IHSrcInfo& getSource() const;
	const IHSrcInfo& getDescendant() const;

	auto operator<=>(const IHSrcInfo& other) const noexcept
	{
		if ((ih == other.ih) && (idx == other.idx))
			return std::strong_ordering::equal;

		if ((ih < other.ih) || ((ih == other.ih) && (idx < other.idx)))
			return std::strong_ordering::less;

		return std::strong_ordering::greater;
	}

	bool operator==(const IHSrcInfo& other) const noexcept;

	explicit operator bool() const noexcept
	{
		return idx != -1;
	}
};

struct IHSrcInfoMulti
{
	Isohypse* ih = nullptr;
	std::vector<int> indices;
	qint64 ihGuid = -1;

	IHSrcInfoMulti() = default;
	IHSrcInfoMulti(const IHSrcInfoMulti&) = default;
	IHSrcInfoMulti(const IHSrcInfo& single);
	IHSrcInfoMulti(Isohypse* inIh, const std::vector<int>& idxs);

	IHSrcInfo first() const;
	IHSrcInfo last() const;
	bool contains(const IHSrcInfo& src) const;

	auto operator<=>(const IHSrcInfoMulti& other) const noexcept = default;
	IHSrcInfo operator[](int idx) const { return { ih, indices[idx] }; }

	explicit operator bool() const noexcept
	{
		return !indices.empty();
	}

private:
	mutable std::vector<QVector3D> lazyPoints;
};

void omniSave(const IHSrcInfo& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(IHSrcInfo& object, OmniBin<std::ios::in>& omniBin);
void omniSave(const IHSrcInfoMulti& object, OmniBin<std::ios::out>& omniBin);
void omniLoad(IHSrcInfoMulti& object, OmniBin<std::ios::in>& omniBin);