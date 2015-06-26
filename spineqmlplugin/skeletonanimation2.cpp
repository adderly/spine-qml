/******************************************************************************
 * Spine Runtimes Software License
 * Version 2.1
 *
 * Copyright (c) 2013, Esoteric Software
 * All rights reserved.
 *
 * You are granted a perpetual, non-exclusive, non-sublicensable and
 * non-transferable license to install, execute and perform the Spine Runtimes
 * Software (the "Software") solely for internal use. Without the written
 * permission of Esoteric Software (typically granted by licensing Spine), you
 * may not (a) modify, translate, adapt or otherwise create derivative works,
 * improvements of the Software or develop new applications using the Software
 * or (b) remove, delete, alter or obscure any trademarks or any copyright,
 * trademark, patent or other intellectual property or proprietary rights
 * notices on or in the Software, including any copy thereof. Redistributions
 * in binary or source form must include this license and terms.
 *
 * THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ESOTERIC SOFTARE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "skeletonanimation2.h"
#include <QQuickWindow>
#include <spine/extension.h>
#include <QFile>
#include <QtQml/QQmlFile>
#include <QSGSimpleRectNode>
#include "texture.h"
#include "skeletonrenderer.h"
#include "rendercmdscache.h"

#define SAFE_DELETE(p) {if(p) { delete (p); (p)=NULL;} }

#ifdef UTIL_S___
//#define UTIL_S 1

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

extern void animationCallback (spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);

static const int quadTriangles[6] = {0, 1, 2, 2, 3, 0};

SkeletonAnimation::SkeletonAnimation(QQuickItem *parent)
    :QQuickItem(parent)
    ,mScale(1.0f)
    ,mSkin("default")
    ,mTimeScale(1.0f)
    ,mPremultipliedAlapha(true)
    ,mDebugSlots(false)
    ,mDebugBones(false)
    ,mShouldRelaseCacheTexture(false)
    ,mSkeletonLoaded(false)
    ,mspSkeleton(0)
    ,mspAtlas(0)
    ,mspRootBone(0)
    ,mspAnimationState(0)
{
    //this->setTextureFollowsItemSize(true);
    mWorldVertices = new float[1000]; // Max number of vertices per mesh.
}

SkeletonAnimation::~SkeletonAnimation()
{
    releaseSkeletonRelatedData();
    delete[] mWorldVertices;
}

void SkeletonAnimation::setToSetupPose()
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::setToSetupPose Error: Skeleton is not ready";
        return;
    }
    spSkeleton_setToSetupPose(mspSkeleton);
}

void SkeletonAnimation::setBonesToSetupPose()
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::setBonesToSetupPose Error: Skeleton is not ready";
        return;
    }
    spSkeleton_setBonesToSetupPose(mspSkeleton);
}

void SkeletonAnimation::setSlotsToSetupPose()
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::setSlotsToSetupPose Error: Skeleton is not ready";
        return;
    }
    spSkeleton_setSlotsToSetupPose(mspSkeleton);
}

bool SkeletonAnimation::setAttachment(const QString &slotName, const QString &attachmentName)
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::setAttachment Error: Skeleton is not ready";
        return false;
    }

    const bool res = spSkeleton_setAttachment(mspSkeleton, slotName.toStdString().c_str(), attachmentName.toStdString().c_str());
    if (res){
        qDebug()<<"SkeletonAnimation::setAttachment Error. slotName"<<slotName<<", attachmentName"<<attachmentName;
    }

    return res;
}

void SkeletonAnimation::setMix(const QString &fromAnimation, const QString &toAnimation, float duration)
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::setMix Error: Skeleton is not ready.";
        return;
    }

    spAnimationStateData_setMixByName(mspAnimationState->data, fromAnimation.toStdString().c_str(), toAnimation.toStdString().c_str(), duration);
}

void SkeletonAnimation::setAnimation(int trackIndex, const QString& name, bool loop)
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::setAnimation Error: Skeleton is not ready";
        return;
    }

    spAnimation* animation = spSkeletonData_findAnimation(mspSkeleton->data, name.toStdString().c_str());
    if (!animation) {
        qDebug()<<"SkeletonAnimation::setAnimation Error: Animation is not found:"<<name;
        return;
    }
    spAnimationState_setAnimation(mspAnimationState, trackIndex, animation, loop);
}

void SkeletonAnimation::addAnimation(int trackIndex, const QString& name, bool loop, float delay)
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::addAnimation Error: Skeleton is not ready";
        return;
    }

    spAnimation* animation = spSkeletonData_findAnimation(mspSkeleton->data, name.toStdString().c_str());
    if (!animation) {
        qDebug()<<"SkeletonAnimation::setAnimation Error: Animation is not found:"<<name;
        return;
    }
    spAnimationState_addAnimation(mspAnimationState, trackIndex, animation, loop, delay);
}

void SkeletonAnimation::clearTracks()
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::clearTracks Error: Skeleton is not ready";
        return;
    }
    spAnimationState_clearTracks(mspAnimationState);
}

void SkeletonAnimation::clearTrack(int trackIndex)
{
    if (!isSkeletonValid()){
        qDebug()<<"SkeletonAnimation::clearTrack Error: Skeleton is not ready";
        return;
    }
    spAnimationState_clearTrack(mspAnimationState, trackIndex);
}

void SkeletonAnimation::setSkeletonDataFile(const QUrl & url)
{
    if (mSkeletonDataFile == url)
        return;
    mSkeletonDataFile = url;
    Q_EMIT skeletonDataFileChanged(mSkeletonDataFile);

    if (isComponentComplete()) {
        loadSkeletonAndAtlasData();
    }
}

void SkeletonAnimation::setAtlasFile(const QUrl & url)
{
    if (mAtlasFile == url)
        return;
    mAtlasFile = url;
    Q_EMIT atlasFileChanged(mAtlasFile);

    if (isComponentComplete()) {
        loadSkeletonAndAtlasData();
    }
}

void SkeletonAnimation::setScale(float value)
{
    if (mScale == value)
        return;
    mScale = value;
    Q_EMIT scaleChanged();
    if (isComponentComplete()) {
        loadSkeletonAndAtlasData();
    }
}

void SkeletonAnimation::setSkin(const QString & value)
{
    if (mSkin == value)
        return;
    mSkin = value;
    Q_EMIT skinChanged();

    if (isSkeletonValid()){
        const bool res = spSkeleton_setSkinByName(mspSkeleton, mSkin.toStdString().c_str());
        if (!res)
            qDebug()<<"SkeletonAnimation::setSkin Error: Invalid skin:"<<mSkin;
    }
}

void SkeletonAnimation::setTimeScale(float value)
{
    if (mTimeScale == value)
        return;
    mTimeScale = value;
    Q_EMIT timeScaleChanged();
}

void SkeletonAnimation::setPremultipliedAlapha(bool value)
{
    if (mPremultipliedAlapha == value)
        return;
    mPremultipliedAlapha = value;
    Q_EMIT premultipliedAlaphaChanged();
}

void SkeletonAnimation::setDebugSlots(bool debug)
{
    if (mDebugSlots == debug)
        return;
    mDebugSlots = debug;
    Q_EMIT debugSlotsChanged();
}

void SkeletonAnimation::setDebugBones(bool debug)
{
    if (mDebugBones == debug)
        return;
    mDebugBones = debug;
    Q_EMIT debugBonesChanged();
}

void SkeletonAnimation::setSourceSize(const QSize & size)
{
    if (mSourceSize == size)
        return;
    mSourceSize = size;
    Q_EMIT sourceSizeChanged();
}

//QQuickFramebufferObject::Renderer *SkeletonAnimation::createRenderer() const
//{
//    return new SkeletonRenderer;
//}

void SkeletonAnimation::renderToCache(void* renderer, RenderCmdsCache* cache)
{
    if (!cache)
        return;
    cache->clear();

    if (!renderer)
        return;

    SkeletonRenderer* skeletonRenderer = (SkeletonRenderer*)renderer;
    if (mShouldRelaseCacheTexture){
        mShouldRelaseCacheTexture = false;
        skeletonRenderer->releaseTextures();
    }

    const QRectF rect = calculateSkeletonRect();

    if (!isSkeletonValid())
        return;

    cache->setSkeletonRect(rect);

    cache->bindShader(RenderCmdsCache::ShaderTexture);
    int additive = -1;
    Color color;
    const float* uvs = 0;
    int verticesCount = 0;
    const int* triangles = 0;
    int trianglesCount = 0;
    float r = 0, g = 0, b = 0, a = 0;
    for (int i = 0, n = mspSkeleton->slotsCount; i < n; i++) {
        spSlot* slot = mspSkeleton->drawOrder[i];
        if (!slot->attachment)
            continue;

        Texture *texture = 0;
        switch (slot->attachment->type) {
        case SP_ATTACHMENT_REGION: {
            spRegionAttachment* attachment = (spRegionAttachment*)slot->attachment;
            spRegionAttachment_computeWorldVertices(attachment, slot->bone, mWorldVertices);
            texture = getTexture(attachment);
            uvs = attachment->uvs;
            verticesCount = 8;
            triangles = quadTriangles;
            trianglesCount = 6;
            r = attachment->r;
            g = attachment->g;
            b = attachment->b;
            a = attachment->a;
            break;
        }
        case SP_ATTACHMENT_MESH: {
            spMeshAttachment* attachment = (spMeshAttachment*)slot->attachment;
            spMeshAttachment_computeWorldVertices(attachment, slot, mWorldVertices);
            texture = getTexture(attachment);
            uvs = attachment->uvs;
            verticesCount = attachment->verticesCount;
            triangles = attachment->triangles;
            trianglesCount = attachment->trianglesCount;
            r = attachment->r;
            g = attachment->g;
            b = attachment->b;
            a = attachment->a;
            break;
        }
        case SP_ATTACHMENT_SKINNED_MESH: {
            spSkinnedMeshAttachment* attachment = (spSkinnedMeshAttachment*)slot->attachment;
            spSkinnedMeshAttachment_computeWorldVertices(attachment, slot, mWorldVertices);
            texture = getTexture(attachment);
            uvs = attachment->uvs;
            verticesCount = attachment->uvsCount;
            triangles = attachment->triangles;
            trianglesCount = attachment->trianglesCount;
            r = attachment->r;
            g = attachment->g;
            b = attachment->b;
            a = attachment->a;
            break;
        }
        default:
            break;
        }// END switch (slot->attachment->type)

        if (texture) {
            if (slot->data->additiveBlending != additive) {
                cache->cacheTriangleDrawCall();
                cache->blendFunc(GL_ONE, slot->data->additiveBlending ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                additive = slot->data->additiveBlending;
            }
            color.a = mspSkeleton->a * slot->a * a * 255;
            float multiplier = mPremultipliedAlapha ? color.a : 255;
            color.r = mspSkeleton->r * slot->r * r * multiplier;
            color.g = mspSkeleton->g * slot->g * g * multiplier;
            color.b = mspSkeleton->b * slot->b * b * multiplier;
            cache->drawTriangles(skeletonRenderer->getGLTexture(texture, window()), mWorldVertices, uvs, verticesCount, triangles, trianglesCount, color);
//            cache->drawTriangles(renderer, mWorldVertices, uvs, verticesCount, triangles, trianglesCount, color);
        }// END if (texture)
    }// END for (int i = 0, n = skeleton->slotsCount; i < n; i++)
    cache->cacheTriangleDrawCall();

    if (mDebugSlots || mDebugBones) {
        cache->bindShader(RenderCmdsCache::ShaderColor);
        if (mDebugSlots) {
            // Slots.
            cache->drawColor(0, 0, 255, 255);
            cache->lineWidth(1);

            Point points[4];
            for (int i = 0, n = mspSkeleton->slotsCount; i < n; i++) {
                spSlot* slot = mspSkeleton->drawOrder[i];
                if (!slot->attachment || slot->attachment->type != SP_ATTACHMENT_REGION) continue;
                spRegionAttachment* attachment = (spRegionAttachment*)slot->attachment;
                spRegionAttachment_computeWorldVertices(attachment, slot->bone, mWorldVertices);
                points[0] = Point(mWorldVertices[0], mWorldVertices[1]);
                points[1] = Point(mWorldVertices[2], mWorldVertices[3]);
                points[2] = Point(mWorldVertices[4], mWorldVertices[5]);
                points[3] = Point(mWorldVertices[6], mWorldVertices[7]);
                cache->drawPoly(points, 4);
            }
        }// END if (mDebugSlots)

        if (mDebugBones) {
            // Bone lengths.
            cache->lineWidth(2);
            cache->drawColor(255, 0, 0, 255);
            for (int i = 0, n = mspSkeleton->bonesCount; i < n; i++) {
                spBone *bone = mspSkeleton->bones[i];
                float x = bone->data->length * bone->m00 + bone->worldX;
                float y = bone->data->length * bone->m10 + bone->worldY;
                cache->drawLine(Point(bone->worldX, bone->worldY), Point(x, y));
            }
            // Bone origins.
            cache->pointSize(4.0);
            cache->drawColor(0, 0, 255, 255); // Root bone is blue.
            for (int i = 0, n = mspSkeleton->bonesCount; i < n; i++) {
                spBone *bone = mspSkeleton->bones[i];
                cache->drawPoint(Point(bone->worldX, bone->worldY));
                if (i == 0) cache->drawColor(0, 255, 0, 255);
            }
        }// END if (mDebugBones)
    }//END if (mDebugSlots || mDebugBones)
}

void SkeletonAnimation::onAnimationStateEvent(int trackIndex, spEventType type, spEvent *event, int loopCount)
{
    switch (type) {
    case SP_ANIMATION_START:
        Q_EMIT skeletonStart(trackIndex);
        break;
    case SP_ANIMATION_END:
        Q_EMIT skeletonEnd(trackIndex);
        break;
    case SP_ANIMATION_COMPLETE:
        Q_EMIT skeletonComplete(trackIndex, loopCount);
        break;
    case SP_ANIMATION_EVENT:
    {
        SpineEvent* spineEvent = new SpineEvent(this);
        spineEvent->setEvent(event);
        mEventCache.push_back(spineEvent);
        Q_EMIT skeletonEvent(trackIndex, spineEvent);
        break;
    }
    }
}

void SkeletonAnimation::onTrackEntryEvent(int /*trackIndex*/, spEventType /*type*/, spEvent* /*event*/, int /*loopCount*/)
{
}

void SkeletonAnimation::updateSkeletonAnimation()
{
    if (!isSkeletonValid()) {
        update();
        return;
    }

    if (!mEventCache.isEmpty()){
        Q_FOREACH(SpineEvent* event, mEventCache)
            SAFE_DELETE(event);
        mEventCache.clear();
    }

    qint64 mSecs = 0;
    if (!mTimer.isValid())
        mTimer.start();
    else
        mSecs = mTimer.restart();

    const float deltaTime = mSecs/1000.0 * mTimeScale;
    spSkeleton_update(mspSkeleton, deltaTime);
    spAnimationState_update(mspAnimationState, deltaTime);
    spAnimationState_apply(mspAnimationState, mspSkeleton);
    spSkeleton_updateWorldTransform(mspSkeleton);

    const QRectF rect = calculateSkeletonRect();
    setSourceSize(QSize(rect.width(), rect.height()));
    setImplicitSize(rect.width(), rect.height());
    setPosition(QPointF(rect.left(), -1.0f*(rect.top() + rect.height())));
    update();
}

void SkeletonAnimation::loadSkeletonAndAtlasData()
{
    releaseSkeletonRelatedData();

    if (mAtlasFile.isEmpty() || !mAtlasFile.isValid()){
        qDebug()<<"SkeletonAnimation::loadSkeletonAndAtlasData Error: Invalid AtlasFile:"<<mAtlasFile;
        return;
    }

    if (mSkeletonDataFile.isEmpty() || !mSkeletonDataFile.isValid()){
        qDebug()<<"SkeletonAnimation::loadSkeletonAndAtlasData Error: Invalid SkeletonDataFile:"<<mSkeletonDataFile;
        return;
    }

    std::string atlasFile = QQmlFile::urlToLocalFileOrQrc(mAtlasFile).toStdString();
    mspAtlas = spAtlas_createFromFile(atlasFile.c_str(), 0);
    if (!mspAtlas){
        qDebug()<<"SkeletonAnimation::loadSkeletonAndAtlasData Error: Atals is null. AtlasFile:"<<mAtlasFile;
        return;
    }

    spSkeletonJson* json = spSkeletonJson_create(mspAtlas);
    json->scale = mScale;
    std::string skeletonFile = QQmlFile::urlToLocalFileOrQrc(mSkeletonDataFile).toStdString();
    spSkeletonData* skeletonData = spSkeletonJson_readSkeletonDataFile(json, skeletonFile.c_str());
    if (!skeletonData){
        qDebug()<<"SkeletonAnimation::loadSkeletonAndAtlasData Error: Failed to load json. jason error:"<<(json->error ? json->error : "null.");
        releaseSkeletonRelatedData();
        return;
    }

    spSkeletonJson_dispose(json);

    mspSkeleton = spSkeleton_create(skeletonData);
    mspRootBone = mspSkeleton->bones[0];

    const bool res = spSkeleton_setSkinByName(mspSkeleton, mSkin.toStdString().c_str());
    if (!res && !mSkin.isEmpty())
        qDebug()<<"SkeletonAnimation::loadSkeletonAndAtlasData Error: Invalid skin:"<<mSkin;

    mspAnimationState = spAnimationState_create(spAnimationStateData_create(mspSkeleton->data));
    mspAnimationState->rendererObject = this;
    mspAnimationState->listener = animationCallback;

    mSkeletonLoaded = true;

    mTimer.invalidate();
}

QRectF SkeletonAnimation::calculateSkeletonRect()
{
    if (!isSkeletonValid())
        return QRectF();

    float minX = FLT_MAX, minY = FLT_MAX, maxX = FLT_MIN, maxY = FLT_MIN;
    for (int i = 0; i < mspSkeleton->slotsCount; ++i) {
        spSlot* slot = mspSkeleton->slots[i];
        if (!slot->attachment)
            continue;

        int verticesCount;
        if (slot->attachment->type == SP_ATTACHMENT_REGION) {
            spRegionAttachment* attachment = (spRegionAttachment*)slot->attachment;
            spRegionAttachment_computeWorldVertices(attachment, slot->bone, mWorldVertices);
            verticesCount = 8;
        } else if (slot->attachment->type == SP_ATTACHMENT_MESH) {
            spMeshAttachment* mesh = (spMeshAttachment*)slot->attachment;
            spMeshAttachment_computeWorldVertices(mesh, slot, mWorldVertices);
            verticesCount = mesh->verticesCount;
        } else if (slot->attachment->type == SP_ATTACHMENT_SKINNED_MESH) {
            spSkinnedMeshAttachment* mesh = (spSkinnedMeshAttachment*)slot->attachment;
            spSkinnedMeshAttachment_computeWorldVertices(mesh, slot, mWorldVertices);
            verticesCount = mesh->uvsCount;
        } else
            continue;

        for (int ii = 0; ii < verticesCount; ii += 2) {
            float x = mWorldVertices[ii], y = mWorldVertices[ii + 1];
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }

    QRectF rect(minX, minY, maxX - minX, maxY - minY);
    return rect;
}

Texture *SkeletonAnimation::getTexture(spRegionAttachment *attachment) const
{
    return (Texture*)((spAtlasRegion*)attachment->rendererObject)->page->rendererObject;
}

Texture *SkeletonAnimation::getTexture(spMeshAttachment *attachment) const
{
    return (Texture*)((spAtlasRegion*)attachment->rendererObject)->page->rendererObject;
}

Texture *SkeletonAnimation::getTexture(spSkinnedMeshAttachment *attachment) const
{
    return (Texture*)((spAtlasRegion*)attachment->rendererObject)->page->rendererObject;
}

bool SkeletonAnimation::isSkeletonValid()
{
    return mSkeletonLoaded && mspSkeleton && mspAnimationState;
}

void SkeletonAnimation::releaseSkeletonRelatedData()
{
    if (mspAnimationState){
        spAnimationStateData_dispose(mspAnimationState->data);
        spAnimationState_dispose(mspAnimationState);
        mspAnimationState = 0;
    }

    if (mspSkeleton)
        spSkeletonData_dispose(mspSkeleton->data);

    if (mspAtlas){
        spAtlas_dispose(mspAtlas);
        mspAtlas = 0;
    }

    if (mspSkeleton) {
        spSkeleton_dispose(mspSkeleton);
        mspSkeleton = 0;
    }

    mSkeletonLoaded = false;
    mShouldRelaseCacheTexture = true;
}

void SkeletonAnimation::componentComplete()
{
    QQuickItem::componentComplete();
    loadSkeletonAndAtlasData();
}

