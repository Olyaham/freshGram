// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2025
#pragma once

#include "ui/widgets/buttons.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Data {
class DocumentMedia;
}

namespace Info::Profile {

struct MusicButtonData;

class AyuMusicButton final : public Ui::RippleButton {
public:
	AyuMusicButton(QWidget *parent, MusicButtonData data, Fn<void()> handler);
	~AyuMusicButton();

	void updateData(MusicButtonData data);

private:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;

	std::unique_ptr<Ui::FlatLabel> _performer;
	std::unique_ptr<Ui::FlatLabel> _title;
	std::shared_ptr<Data::DocumentMedia> _mediaView;

};

} // namespace Info::Profile
