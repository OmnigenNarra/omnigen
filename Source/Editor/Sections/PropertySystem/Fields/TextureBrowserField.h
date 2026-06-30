#pragma once
#include <QImage>
#include <QLabel>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include "Omnigen.h"
#include "FieldImplBase.h"

class QTextureBrowserFieldImpl : public QFieldImplBase
{
    inline static QHash<QImageData*, QImage> cachedPreviews;

public:
    QTextureBrowserFieldImpl()
        : imageLabel(new QLabel)
    {
        setLayout(new QHBoxLayout());
        layout()->addWidget(imageLabel);

        auto* browseButton = new QPushButton("browse");
        connect(browseButton, &QPushButton::clicked, this, [this]()
            {
                QDir dir = Omnigen::get()->getProjectDir();

                QString newTexture = dir.relativeFilePath(QFileDialog::getOpenFileName(this, tr("Choose texture"), "", tr("Images (*.png *.jpg)")));
                if (!newTexture.isEmpty())
                    texture = QImage(newTexture);

                loadPreview();
                emit valueChanged();
            });

        layout()->addWidget(browseButton);
    }

    const auto& getTexture() const { return texture; }

    void setTexture(const QImage& inTexture)
    {
        texture = inTexture;
        loadPreview();
    }

private:
    void loadPreview()
    {
        if (!cachedPreviews.contains(texture.data_ptr()))
            cachedPreviews[texture.data_ptr()] = texture.scaledToHeight(50);

        imageLabel->setPixmap(QPixmap::fromImage(cachedPreviews[texture.data_ptr()]));
    }

    QLabel* imageLabel = nullptr;
    QImage texture;
};

template<typename T>
class TextureBrowserField : public QTextureBrowserFieldImpl
{
    static_assert(std::is_same_v<T, QImage>);

public:
    void set(const QImage& newValue)
    {
        setTexture(newValue);
    }

    const QImage& get()
    {
        return getTexture();
    }
};