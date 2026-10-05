// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "rpl/producer.h"
#include "ui/text/text_entity.h"

class QImage;
class QWidget;

namespace AyuFeatures::StreamerMode {

void apply(bool enabled);
void hideWidgetWindow(QWidget *widget);
void showWidgetWindow(QWidget *widget);

[[nodiscard]] bool spoilersActive();
[[nodiscard]] rpl::producer<bool> spoilersActiveValue();
[[nodiscard]] rpl::producer<TextWithEntities> spoilered(
	rpl::producer<TextWithEntities> text);
void spoilerImage(QImage &image);

} // namespace AyuFeatures::StreamerMode
