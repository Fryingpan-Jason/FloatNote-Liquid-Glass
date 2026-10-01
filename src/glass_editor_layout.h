#pragma once
#include <algorithm>
#include <cmath>

// Screen-pixel geometry only. Text, selection and undo remain in the native EDIT.
namespace GlassEditorLayout {
struct Box {
    int left, top, right, bottom;
    int Width() const { return right-left; }
    int Height() const { return bottom-top; }
};
struct Layout { Box editor, grip; };

inline Layout Calculate(int width,int height,float radiusDip,float scale) {
    scale=std::max(.5f,scale);
    auto px=[&](float dip){return static_cast<int>(std::lround(dip*scale));};
    const float radius=std::clamp(radiusDip*scale,0.0f,std::min(width,height)*.5f);
    const int gripSize=std::max(1,px(18));
    const int gripInset=std::max(px(std::max(22.0f,std::ceil(radius/scale*.3f)+18)),
        gripSize+static_cast<int>(std::ceil(radius*.292894f))+std::max(1,px(1)));
    Box grip{width-gripInset,height-gripInset,width-gripInset+gripSize,height-gripInset+gripSize};
    if(height<=px(48)) {
        // A shallow capsule has no usable bottom-right square. Keep its native
        // resize handle centered vertically and inset to the actual contour.
        const int top=std::max(0,(height-gripSize)/2);
        const int edgeY=std::min(top,height-top-gripSize);
        const float guard=static_cast<float>(std::max(1,px(1)));
        const float dy=std::max(0.0f,radius-std::max(0.0f,edgeY-guard));
        const int inset=static_cast<int>(std::ceil(radius-std::sqrt(
            std::max(0.0f,radius*radius-dy*dy))+guard));
        grip={width-inset-gripSize,top,width-inset,top+gripSize};
    }

    const int inscribed=static_cast<int>(std::ceil(radius*.292894f))+px(3);
    // Spacious notes gain a little breathing room; small or shallow notes keep
    // the full text height. The interpolation avoids jumps while resizing.
    const float space=std::clamp(std::min((width/scale-240.0f)/240.0f,
                                         (height/scale-140.0f)/180.0f),0.0f,1.0f);
    const int padding=px(12+6*space);
    // Recover vertical space on shallow cards, but move padding by at most
    // 6 DIP on large circles so their editor does not become a narrow column.
    const int vertical=std::max(padding,inscribed-px(6));
    const float guard=static_cast<float>(px(2));
    const float innerY=std::max(0.0f,vertical-guard);
    const float dy=std::max(0.0f,radius-innerY);
    const int horizontal=std::max(padding,static_cast<int>(std::ceil(
        radius-std::sqrt(std::max(0.0f,radius*radius-dy*dy))+guard)));
    const int left=std::min(horizontal,std::max(0,width-2));
    const int top=std::min(vertical,std::max(0,height-2));
    // The grip occupies only a right-side gutter, not a full-width bottom row.
    const int right=std::max(left+1,std::min(width-horizontal,grip.left-px(2)));
    const int bottom=std::max(top+1,height-vertical);
    return {{left,top,right,bottom},grip};
}
}
