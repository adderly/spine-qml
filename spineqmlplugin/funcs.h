#ifndef FUNCS
#define FUNCS

#ifndef UTIL_S
#define UTIL_S

#include "skeletonanimationfbo.h"
#include <QQuickWindow>
#include <spine/extension.h>
#include <QFile>
#include <QtQml/QQmlFile>
#include <QSGSimpleRectNode>
#include "texture.h"
#include "skeletonrenderer.h"
#include "rendercmdscache.h"

 void _spAtlasPage_createTexture (spAtlasPage* self, const char* path) {
    Texture* texture = new Texture(path);
    self->rendererObject = texture;
    self->width = texture->size().width();
    self->height = texture->size().height();
}

 void _spAtlasPage_disposeTexture (spAtlasPage* self) {
    if (self->rendererObject){
        Texture* texture = (Texture*)self->rendererObject;
        self->rendererObject = 0;
        delete texture;
    }
}

 char* _spUtil_readFile (const char* path, int* length) {
    if (!path){
        qDebug()<<"_spUtil_readFile Error: Null path";
        return 0;
    }

    QFile file(path);
    if (!file.exists()){
        qDebug()<<"_spUtil_readFile Error: File not exists. path:"<<path;
        return 0;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        qDebug()<<"_spUtil_readFile Error: Failed to open file. path:"<<path;
        return 0;
    }

    QByteArray data = file.readAll();
    file.close();

    *length = data.size();
    char* bytes = MALLOC(char, *length);
    memcpy(bytes, data.constData(), *length);
    return bytes;
}

 void animationCallback (spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount) {
    ((SkeletonAnimation*)state->rendererObject)->onAnimationStateEvent(trackIndex, type, event, loopCount);
}

 void trackEntryCallback (spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount) {
    ((SkeletonAnimation*)state->rendererObject)->onTrackEntryEvent(trackIndex, type, event, loopCount);
}

#endif

#endif // FUNCS

