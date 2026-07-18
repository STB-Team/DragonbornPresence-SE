#pragma once

#include <optional>
#include <ranges>
#include <string>
#include <type_traits>

class ScriptUtils
{
private:
    template <class TForm>
        requires std::is_base_of_v<RE::TESForm, TForm> ||
                 std::same_as<RE::ActiveEffect, TForm> ||
                 std::same_as<RE::BGSBaseAlias, TForm>
    static std::optional<RE::BSScript::Variable*> GetPropertyOrVariable(
        const TForm* form,
        const std::string& scriptName,
        const std::string& propertyName)
    {
        if (!form) return std::nullopt;

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        if (!vm || !policy) return std::nullopt;

        RE::VMTypeID formType;
        if constexpr (std::is_base_of_v<RE::TESForm, TForm>) {
            formType = form->formType;
        } else if constexpr (std::same_as<RE::ActiveEffect, TForm>) {
            formType = RE::ActiveEffect::VMTYPEID;
        } else {
            formType = form->GetVMTypeID();
        }

        const RE::VMHandle handle = policy->GetHandleForObject(formType, form);
        if (handle == policy->EmptyHandle()) return std::nullopt;

        RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
        const auto scripts = vm->attachedScripts.find(handle);
        if (scripts == vm->attachedScripts.end()) return std::nullopt;

        for (const auto& script : scripts->second) {
            if (!script || !script->GetTypeInfo() ||
                std::string_view(script->GetTypeInfo()->name.c_str()) !=
                    std::string_view(scriptName)) {
                continue;
            }
            if (auto* value = script->GetProperty(propertyName)) return value;
            if (auto* value = script->GetVariable(propertyName)) return value;
            return std::nullopt;
        }
        return std::nullopt;
    }

    template <class T>
    static T GetVariableValue(const RE::BSScript::Variable& variable)
    {
        if constexpr (std::same_as<T, bool>) {
            return variable.GetBool();
        } else if constexpr (std::is_signed_v<T> || std::is_enum_v<T>) {
            return static_cast<T>(variable.GetSInt());
        } else if constexpr (std::is_unsigned_v<T>) {
            return static_cast<T>(variable.GetUInt());
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(variable.GetFloat());
        } else if constexpr (std::same_as<T, std::string>) {
            return variable.GetString();
        } else {
            static_assert(!sizeof(T), "Unsupported Papyrus value type");
        }
    }

public:
    template <class T, class TForm>
    static std::optional<T> GetScriptPropertyOrVariable(
        const TForm* form,
        const std::string& scriptName,
        const std::string& propertyName)
    {
        return GetPropertyOrVariable(form, scriptName, propertyName)
            .transform([](auto* value) { return GetVariableValue<T>(*value); });
    }

    template <class T>
    static std::optional<T> GetFirstAliasScriptPropertyOrVariable(
        const RE::TESQuest* quest,
        const std::string& scriptName,
        const std::string& propertyName)
    {
        if (!quest) return std::nullopt;
        const auto alias = std::ranges::find_if(quest->aliases, [](auto* value) {
            return value != nullptr;
        });
        return alias != quest->aliases.end()
            ? GetScriptPropertyOrVariable<T>(*alias, scriptName, propertyName)
            : std::nullopt;
    }
};
