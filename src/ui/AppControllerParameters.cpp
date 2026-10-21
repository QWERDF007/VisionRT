#include "ui/AppController_p.h"

#include <visionrt/sdk/ValueYaml.h>

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace visionrt {

bool AppController::Impl::applyParameter(const QString &id, bool initialization, PortValue value)
{
    const auto *node = currentNode();
    if (!node)
        throw ContractError("No selected node");
    const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
    const auto signature = signatures.find(id.toStdString());
    if (signature == signatures.end())
        throw ContractError("Unknown parameter");
    if (signature->second.type == Type::Float && !isMissing(value) && !std::isfinite(std::get<double>(value)))
        throw ContractError("Numeric parameter must be finite");
    if (const auto *declaration = parameter(id.toStdString(), initialization); declaration && !isMissing(value))
    {
        const auto less = [&](const PortValue &left, const PortValue &right)
        {
            return signature->second.type == Type::Int
                     ? std::get<std::int64_t>(left) < std::get<std::int64_t>(right)
                     : std::get<double>(left) < std::get<double>(right);
        };
        if ((declaration->minimum && less(value, *declaration->minimum))
            || (declaration->maximum && less(*declaration->maximum, value)))
            throw ContractError("Value is outside the declared range");
        if (!declaration->options.empty()
            && std::none_of(declaration->options.begin(), declaration->options.end(),
                            [&](const auto &option) { return equalValues(value, option); }))
            throw ContractError("Value is not a declared option");
    }
    return initialization
             ? editor.setInitParam(flow.toStdString(), node->id, id.toStdString(), std::move(value))
             : editor.setLiteral(flow.toStdString(), node->id, id.toStdString(), std::move(value));
}

bool AppController::setParameter(const QString &id, bool initialization, const QString &yamlText)
{
    return impl_->command([&]
    {
        const auto *node = impl_->currentNode();
        if (!node)
            throw ContractError("No selected node");
        const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
        const auto signature = signatures.find(id.toStdString());
        if (signature == signatures.end())
            throw ContractError("Unknown parameter");

        PortValue value;
        const std::string text = yamlText.toStdString();
        YAML::Node parsed;
        try { parsed = YAML::Load(text); } catch (...) {}

        if (!parsed.IsDefined() || parsed.IsNull())
        {
            if (signature->second.type == Type::RoiBatch)
                value = RoiBatch{};
            else
                value = Missing{};
        }
        else if (signature->second.type == Type::String)
        {
            if (parsed.IsScalar())
                value = Utf8(parsed.Scalar());
            else
                value = Utf8(text);
        }
        else if (signature->second.type == Type::Rect)
        {
            const std::string trimmed = yamlText.trimmed().toStdString();
            if (trimmed.empty() || trimmed == "[]" || trimmed == "null" || trimmed == "~")
            {
                value = Missing{};
            }
            else
            {
                bool parsedAsYaml = false;
                try
                {
                    parsed = YAML::Load(trimmed);
                    if (parsed.IsSequence())
                    {
                        std::vector<float> nums;
                        for (const auto &item : parsed)
                            nums.push_back(item.as<float>());
                        if (nums.size() >= 6 && static_cast<int>(nums[0]) == 2)
                            value = Roi::rectangle(nums[1], nums[2], nums[3], nums[4], nums[5]);
                        else if (nums.size() >= 5)
                            value = Roi::rectangle(nums[0], nums[1], nums[2], nums[3], nums[4]);
                        else if (nums.size() >= 4)
                            value = Roi::rectangle(nums[0], nums[1], nums[2], nums[3], 0.0f);
                        else
                            value = decodeValue(parsed, signature->second);
                        parsedAsYaml = true;
                    }
                }
                catch (...) {}

                if (!parsedAsYaml)
                {
                    QString qtext = yamlText;
                    qtext.replace('[', ' ').replace(']', ' ');
                    const auto parts = qtext.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
                    std::vector<float> nums;
                    bool ok = true;
                    for (const auto &p : parts)
                    {
                        bool conv = false;
                        float val = p.toFloat(&conv);
                        if (!conv) { ok = false; break; }
                        nums.push_back(val);
                    }
                    if (ok && nums.size() >= 6 && static_cast<int>(nums[0]) == 2)
                        value = Roi::rectangle(nums[1], nums[2], nums[3], nums[4], nums[5]);
                    else if (ok && nums.size() >= 5)
                        value = Roi::rectangle(nums[0], nums[1], nums[2], nums[3], nums[4]);
                    else if (ok && nums.size() >= 4)
                        value = Roi::rectangle(nums[0], nums[1], nums[2], nums[3], 0.0f);
                    else
                        value = decodeValue(YAML::Load(trimmed), signature->second);
                }
            }
        }
        else if (signature->second.type == Type::RoiBatch)
        {
            const std::string trimmed = yamlText.trimmed().toStdString();
            if (trimmed.empty() || trimmed == "[]" || trimmed == "null" || trimmed == "~")
                value = RoiBatch{};
            else
                value = decodeValue(YAML::Load(trimmed), signature->second);
        }
        else
        {
            value = decodeValue(YAML::Load(yamlText.toStdString()), signature->second);
        }
        return impl_->applyParameter(id, initialization, std::move(value));
    });
}

bool AppController::setRectParameter(const QString &id, bool initialization, double x, double y, double width,
                                    double height, double angle)
{
    return impl_->command([&]
    {
        return impl_->applyParameter(id, initialization,
                                     Roi::rectangle(static_cast<float>(x), static_cast<float>(y),
                                                    static_cast<float>(width), static_cast<float>(height),
                                                    static_cast<float>(angle)));
    });
}

bool AppController::setParameterPath(const QString &id, bool initialization, const QUrl &url)
{
    return impl_->command([&]
    {
        if (!url.isLocalFile())
            throw ContractError("A local file or directory URL is required");
        return impl_->applyParameter(id, initialization, Utf8(url.toLocalFile().toUtf8().toStdString()));
    });
}

bool AppController::stepIntegerParameter(const QString &id, bool initialization, int direction,
                                        const QString &literal)
{
    if (direction != 1 && direction != -1)
        return false;
    return impl_->command([&]
    {
        const auto *node = impl_->currentNode();
        if (!node)
            throw ContractError("No selected node");
        const auto &signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
        const auto signature = signatures.find(id.toStdString());
        if (signature == signatures.end() || signature->second.type != Type::Int)
            throw ContractError("Numeric parameter is not an integer");
        if (!initialization && node->inputs.at(id.toStdString()).mode != BindingMode::Literal)
            throw ContractError("Upstream integer parameter cannot be stepped");
        const auto current = parseInt64(literal.toStdString());
        std::int64_t step = 1;
        if (const auto *declaration = impl_->parameter(id.toStdString(), initialization);
            declaration && declaration->step)
        {
            const auto *declared = std::get_if<std::int64_t>(&*declaration->step);
            if (!declared || *declared <= 0)
                throw ContractError("Integer step must be positive");
            step = *declared;
        }
        if ((direction > 0 && current > std::numeric_limits<std::int64_t>::max() - step)
            || (direction < 0 && current < std::numeric_limits<std::int64_t>::min() + step))
            throw ContractError("Integer step is outside the int64 range");
        return impl_->applyParameter(id, initialization, current + direction * step);
    });
}

}
