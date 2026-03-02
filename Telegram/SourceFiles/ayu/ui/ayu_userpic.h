// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/userpic_view.h"

namespace Media::Streaming { struct FrameRequest; }

namespace AyuUserpic {

[[nodiscard]] bool ShouldOverrideShape(Ui::PeerUserpicShape shape);
[[nodiscard]] int ComputeRadius(int pixelSize);
[[nodiscard]] double ComputeRadiusF(double size);
[[nodiscard]] bool IsCircle();

[[nodiscard]] uint8 PackedState();

void PaintShape(QPainter &p, int x, int y, int size);
void PaintShape(QPainter &p, const QRectF &rect);

[[nodiscard]] QPointF OnlineBadgePosition(int photoSize, double badgeSize, double stroke = 0);

void ApplyFrameRounding(
	::Media::Streaming::FrameRequest &request,
	std::array<QImage, 4> &cornersCache,
	QImage &ellipseCache,
	QSize size);

} // namespace AyuUserpic
