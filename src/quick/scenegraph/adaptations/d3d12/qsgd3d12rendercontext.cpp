/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQuick module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsgd3d12rendercontext_p.h"
#include "qsgd3d12renderer_p.h"
#include "qsgd3d12texture_p.h"

QT_BEGIN_NAMESPACE

// NOTE: Avoid categorized logging. It is slow.

#define DECLARE_DEBUG_VAR(variable) \
    static bool debug_ ## variable() \
    { static bool value = qgetenv("QSG_RENDERER_DEBUG").contains(QT_STRINGIFY(variable)); return value; }

DECLARE_DEBUG_VAR(render)

QSGD3D12RenderContext::QSGD3D12RenderContext(QSGContext *ctx)
    : QSGRenderContext(ctx)
{
}

void QSGD3D12RenderContext::invalidate()
{
    if (Q_UNLIKELY(debug_render()))
        qDebug("rendercontext invalidate engine %p, %d/%d/%d", m_engine,
               m_texturesToDelete.count(), m_textures.count(), m_fontEnginesToClean.count());

    qDeleteAll(m_texturesToDelete);
    m_texturesToDelete.clear();

    qDeleteAll(m_textures);
    m_textures.clear();

    for (QSet<QFontEngine *>::const_iterator it = m_fontEnginesToClean.constBegin(),
         end = m_fontEnginesToClean.constEnd(); it != end; ++it) {
        (*it)->clearGlyphCache(m_engine);
        if (!(*it)->ref.deref())
            delete *it;
    }
    m_fontEnginesToClean.clear();

    m_sg->renderContextInvalidated(this);
    emit invalidated();
}

QSGTexture *QSGD3D12RenderContext::createTexture(const QImage &image, uint flags) const
{
    Q_ASSERT(m_engine);
    QSGD3D12Texture *t = new QSGD3D12Texture(m_engine);
    t->setImage(image, flags);
    return t;
}

 QSGRenderer *QSGD3D12RenderContext::createRenderer()
{
    return new QSGD3D12Renderer(this);
}

void QSGD3D12RenderContext::renderNextFrame(QSGRenderer *renderer, uint fbo)
{
    static_cast<QSGD3D12Renderer *>(renderer)->renderScene(fbo);
}

QT_END_NAMESPACE
