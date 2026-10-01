// ParamEdit.hpp — a minimal "edit a plugin param" callback for composite widgets that drive more
// than one param (the Ratio knob's hidden Fine, and later the graphical envelope's five handles).
// The UI implements it with DPF's editParameter(begin) / setParameterValue / editParameter(end),
// so hosts see a proper begin/change/end gesture per param for automation recording.

#ifndef FEM_PARAMEDIT_HPP
#define FEM_PARAMEDIT_HPP

#include <cstdint>

namespace fem {

class ParamEditCallback
{
public:
    virtual ~ParamEditCallback() {}
    virtual void paramEditBegin(uint32_t paramId) = 0;
    virtual void paramEditChange(uint32_t paramId, float value) = 0;
    virtual void paramEditEnd(uint32_t paramId) = 0;
};

} // namespace fem

#endif // FEM_PARAMEDIT_HPP
