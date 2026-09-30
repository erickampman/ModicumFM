// QuadStereoMixerModule.cpp — see .hpp.

#include "CPPProcs.hpp"   // panValue()

#include "QuadStereoMixerModule.hpp"
#include "pm_params.h"

const KPBuffPair* ProcessQuadStereoMixerModule(uint64_t processCallID, uint32_t count, Module* module)
{
    // This form passes in zero buffers if the input isn't connected.
    auto in1Pair = module->retrieveInput(QuadStereoMixerParamItemInput1, processCallID, count);
    auto in2Pair = module->retrieveInput(QuadStereoMixerParamItemInput2, processCallID, count);
    auto in3Pair = module->retrieveInput(QuadStereoMixerParamItemInput3, processCallID, count);
    auto in4Pair = module->retrieveInput(QuadStereoMixerParamItemInput4, processCallID, count);

    const AUValue auLevel1 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemLevel1));
    const AUValue auLevel2 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemLevel2));
    const AUValue auLevel3 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemLevel3));
    const AUValue auLevel4 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemLevel4));

    const AUValue auPan1 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemPan1));
    const AUValue auPan2 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemPan2));
    const AUValue auPan3 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemPan3));
    const AUValue auPan4 = module->paramAtIndex(PARAM_ENUM_TO_INDEX(QuadStereoMixerParamItemPan4));

    float *lin1 = in1Pair->first->data;
    float *rin1 = in1Pair->second->data;
    float *lin2 = in2Pair->first->data;
    float *rin2 = in2Pair->second->data;
    float *lin3 = in3Pair->first->data;
    float *rin3 = in3Pair->second->data;
    float *lin4 = in4Pair->first->data;
    float *rin4 = in4Pair->second->data;

    float *lin1opl = lin1 + count;
    float *outlp = module->output.first->data;
    float *outrp = module->output.second->data;

    const float lvlL1 = panValue(auPan1) * auLevel1;
    const float lvlR1 = panValue(-auPan1) * auLevel1;
    const float lvlL2 = panValue(auPan2) * auLevel2;
    const float lvlR2 = panValue(-auPan2) * auLevel2;
    const float lvlL3 = panValue(auPan3) * auLevel3;
    const float lvlR3 = panValue(-auPan3) * auLevel3;
    const float lvlL4 = panValue(auPan4) * auLevel4;
    const float lvlR4 = panValue(-auPan4) * auLevel4;

    for (; lin1 < lin1opl;
         ++lin1, ++rin1,
         ++lin2, ++rin2,
         ++lin3, ++rin3,
         ++lin4, ++rin4,
         ++outlp, ++outrp
         )
    {
        *outlp = lvlL1 * *lin1 + lvlL2 * *lin2 + lvlL3 * *lin3 + lvlL4 * *lin4;
        *outrp = lvlR1 * *rin1 + lvlR2 * *rin2 + lvlR3 * *rin3 + lvlR4 * *rin4;
    }

    return &module->output;
}
