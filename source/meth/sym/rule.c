#include <meth/sym/expr.h>

static const char *const rule_names [ML_RULE_COUNT] = {
	[ML_RULE_NONE]                   = "",
	[ML_RULE_SIMPLIFY]               = "simplify",
	[ML_RULE_ADD_NUMBERS]            = "add the numbers",
	[ML_RULE_COLLECT_TERMS]          = "collect like terms",
	[ML_RULE_MULTIPLY_NUMBERS]       = "multiply the numbers",
	[ML_RULE_MULTIPLY_OUT]           = "multiply out",
	[ML_RULE_COMBINE_FACTORS]        = "combine the factors",
	[ML_RULE_EXPAND_POWER]           = "expand the power",
	[ML_RULE_SIMPLIFY_POWER]         = "simplify the power",
	[ML_RULE_EVALUATE_FUNCTION]      = "evaluate the function",
	[ML_RULE_DIFF_CONSTANT]          = "constant",
	[ML_RULE_DIFF_SUM]               = "sum rule",
	[ML_RULE_DIFF_CONSTANT_MULTIPLE] = "constant multiple rule",
	[ML_RULE_DIFF_PRODUCT]           = "product rule",
	[ML_RULE_DIFF_POWER]             = "power rule",
	[ML_RULE_DIFF_EXPONENTIAL]       = "exponential rule",
	[ML_RULE_DIFF_GENERAL_POWER]     = "general power rule",
	[ML_RULE_DIFF_CHAIN]             = "chain rule",
	[ML_RULE_INT_CONSTANT]           = "constant",
	[ML_RULE_INT_RECIPROCAL]         = "reciprocal",
	[ML_RULE_INT_POWER]              = "power rule",
	[ML_RULE_INT_EXPONENTIAL]        = "exponential",
	[ML_RULE_INT_SINE]               = "sine",
	[ML_RULE_INT_COSINE]             = "cosine",
	[ML_RULE_INT_TANGENT]            = "tangent",
	[ML_RULE_INT_LOGARITHM]          = "logarithm",
	[ML_RULE_INT_ARCTANGENT]         = "arctangent",
	[ML_RULE_INT_PARTIAL_FRACTIONS]  = "partial fractions",
	[ML_RULE_INT_SUBSTITUTION]       = "substitution",
	[ML_RULE_INT_PARTS]              = "integration by parts",
	[ML_RULE_INT_HALF_ANGLE]         = "half angle formula"
};

const char *ml_rule_name (MlRule rule)
{
	int32_t known = (int32_t)rule >= 0 && rule < ML_RULE_COUNT;

	return ml_choose (known, rule_names [ml_choose (known, rule, 0)], "");
}
