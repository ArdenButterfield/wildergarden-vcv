//
// Created by arden on 2/12/25.
//

#ifndef WILDERGARDEN_VCV_WILDERGARDENSWIDGETS_H
#define WILDERGARDEN_VCV_WILDERGARDENSWIDGETS_H

#include "plugin.hpp"

struct FourmsSliderHorizontal : rack::app::SvgSlider {
    FourmsSliderHorizontal() {
        horizontal = true;
        maxHandlePos = rack::mm2px(rack::math::Vec(22.078, 0.738));
        minHandlePos = rack::mm2px(rack::math::Vec(0.738, 0.738));
        setBackgroundSvg(
                rack::Svg::load(rack::asset::plugin(pluginInstance, "res/components/FourmsSliderHorizontal.svg")));
        setHandleSvg(rack::Svg::load(rack::asset::plugin(pluginInstance, "res/components/FourmsSliderHorizontalHandle.svg")));
    }
};


#endif //WILDERGARDEN_VCV_WILDERGARDENSWIDGETS_H
