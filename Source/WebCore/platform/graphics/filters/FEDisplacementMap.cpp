/*
 * Copyright (C) 2004, 2005, 2006, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005 Rob Buis <buis@kde.org>
 * Copyright (C) 2005 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright (C) 2021-2022 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "FEDisplacementMap.h"

#include "FEDisplacementMapSoftwareApplier.h"
#include "Filter.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

Ref<FEDisplacementMap> FEDisplacementMap::create(ChannelSelectorType xChannelSelector, ChannelSelectorType yChannelSelector, float scale, DestinationColorSpace colorSpace)
{
    return adoptRef(*new FEDisplacementMap(xChannelSelector, yChannelSelector, scale, colorSpace));
}

FEDisplacementMap::FEDisplacementMap(ChannelSelectorType xChannelSelector, ChannelSelectorType yChannelSelector, float scale, DestinationColorSpace colorSpace)
    : FilterEffect(FilterEffect::Type::FEDisplacementMap, colorSpace)
    , m_xChannelSelector(xChannelSelector)
    , m_yChannelSelector(yChannelSelector)
    , m_scale(scale)
{
}

bool FEDisplacementMap::operator==(const FEDisplacementMap& other) const
{
    return FilterEffect::operator==(other)
        && m_xChannelSelector == other.m_xChannelSelector
        && m_yChannelSelector == other.m_yChannelSelector
        && m_scale == other.m_scale;
}

bool FEDisplacementMap::setXChannelSelector(const ChannelSelectorType xChannelSelector)
{
    if (m_xChannelSelector == xChannelSelector)
        return false;
    m_xChannelSelector = xChannelSelector;
    return true;
}

bool FEDisplacementMap::setYChannelSelector(const ChannelSelectorType yChannelSelector)
{
    if (m_yChannelSelector == yChannelSelector)
        return false;
    m_yChannelSelector = yChannelSelector;
    return true;
}

bool FEDisplacementMap::setScale(float scale)
{
    if (m_scale == scale)
        return false;
    m_scale = scale;
    return true;
}

FloatRect FEDisplacementMap::calculateImageRect(const Filter& filter, std::span<const FloatRect>, const FloatRect& primitiveSubregion) const
{
    return filter.maxEffectRect(primitiveSubregion);
}

const DestinationColorSpace& FEDisplacementMap::resultColorSpace(std::span<const Ref<FilterImage>> inputs) const
{
    // Spec: The 'color-interpolation-filters' property only applies to the 'in2' source image
    // and does not apply to the 'in' source image. The 'in' source image must remain in its
    // current color space.
    // The result is in that same color space because it is a displacement of the 'in' image.
    return inputs[0]->colorSpace();
}

void FEDisplacementMap::transformInputsColorSpace(std::span<const Ref<FilterImage>> inputs) const
{
    // Do not transform the first primitive input, as per the spec.
    ASSERT(inputs.size() == 2);
    inputs[1]->transformToColorSpace(operatingColorSpace());
}

std::unique_ptr<FilterEffectApplier> FEDisplacementMap::createSoftwareApplier() const
{
    return FilterEffectApplier::create<FEDisplacementMapSoftwareApplier>(*this);
}

static TextStream& operator<<(TextStream& ts, const ChannelSelectorType& type)
{
    switch (type) {
    case ChannelSelectorType::CHANNEL_UNKNOWN:
        ts << "UNKNOWN"_s;
        break;
    case ChannelSelectorType::CHANNEL_R:
        ts << "RED"_s;
        break;
    case ChannelSelectorType::CHANNEL_G:
        ts << "GREEN"_s;
        break;
    case ChannelSelectorType::CHANNEL_B:
        ts << "BLUE"_s;
        break;
    case ChannelSelectorType::CHANNEL_A:
        ts << "ALPHA"_s;
        break;
    }
    return ts;
}

TextStream& FEDisplacementMap::externalRepresentation(TextStream& ts, FilterRepresentation representation) const
{
    ts << indent << "[feDisplacementMap"_s;
    FilterEffect::externalRepresentation(ts, representation);

    ts << " scale=\"" << m_scale << '"';
    ts << " xChannelSelector=\"" << m_xChannelSelector << '"';
    ts << " yChannelSelector=\"" << m_yChannelSelector << '"';

    ts << "]\n"_s;
    return ts;
}

} // namespace WebCore
