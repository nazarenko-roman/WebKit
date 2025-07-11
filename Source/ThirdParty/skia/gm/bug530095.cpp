/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gm/gm.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypes.h"
#include "include/effects/SkDashPathEffect.h"

DEF_SIMPLE_GM(bug530095, canvas, 900, 1200) {
    SkPath path1 = SkPath::Circle(200, 200, 124),
           path2 = SkPath::Circle(2, 2, 1.24f);

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(26);
    SkScalar intervals[] = {700, 700 };
    paint.setPathEffect(SkDashPathEffect::Make(intervals, -40));
    canvas->drawPath(path1, paint);

    paint.setStrokeWidth(0.26f);
    SkScalar smIntervals[] = {7, 7 };
    paint.setPathEffect(SkDashPathEffect::Make(smIntervals, -0.40f));
    canvas->save();
    canvas->scale(100, 100);
    canvas->translate(4, 0);
    canvas->drawPath(path2, paint);
    canvas->restore();

    paint.setStrokeWidth(26);
    paint.setPathEffect(SkDashPathEffect::Make(intervals, 0));
    canvas->save();
    canvas->translate(0, 400);
    canvas->drawPath(path1, paint);
    canvas->restore();

    paint.setStrokeWidth(0.26f);
    paint.setPathEffect(SkDashPathEffect::Make(smIntervals, 0));
    canvas->scale(100, 100);
    canvas->translate(4, 4);
    canvas->drawPath(path2, paint);
}

DEF_SIMPLE_GM(bug591993, canvas, 40, 140) {
    SkPaint p;
    p.setColor(SK_ColorRED);
    p.setAntiAlias(true);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeCap(SkPaint::kRound_Cap);
    p.setStrokeWidth(10);
    const SkScalar intervals[] = { 100, 100 };
    p.setPathEffect(SkDashPathEffect::Make(intervals, 100));
    canvas->drawLine(20, 20, 120, 20, p);
}
