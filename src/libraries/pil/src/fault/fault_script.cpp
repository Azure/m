// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <m/strings/compare.h>
#include <m/strings/convert.h>
#include <m/utility/exception.h>

#include "fault.h"

using namespace std::string_view_literals;

namespace m::pil::impl::fault
{
    namespace
    {
        // Case-insensitive equality of two registry-style strings (paths and
        // value names compare case-insensitively, matching registry semantics).
        bool
        ci_equal(std::u16string_view l, std::u16string_view r)
        {
            m::case_insensitive_less<std::u16string_view> const less{};
            return !less(l, r) && !less(r, l);
        }

        // Raise the exception mapped to a fired rule's action. Each action maps
        // to the m exception the real platform raises for that status, so a
        // fault-injection consumer exercises its true error-handling path.
        [[noreturn]] void
        raise(fault_action action)
        {
            switch (action)
            {
            case fault_action::not_found:
                throw m::not_found("fault injection: not found");
            case fault_action::access_denied:
                throw m::access_denied("fault injection: access denied");
            case fault_action::out_of_resources:
                throw m::out_of_resources("fault injection: out of resources");
            case fault_action::sharing_violation:
                throw m::sharing_violation("fault injection: sharing violation");
            case fault_action::already_exists:
                throw m::already_exists("fault injection: already exists");
            case fault_action::not_supported:
                throw m::not_supported("fault injection: not supported");
            }

            throw m::invalid_parameter("fault injection: unknown action");
        }

        fault_operation
        operation_from_string(std::u16string_view s)
        {
            if (s == u"create_key"sv)
                return fault_operation::create_key;
            if (s == u"open_key"sv)
                return fault_operation::open_key;
            if (s == u"delete_key"sv)
                return fault_operation::delete_key;
            if (s == u"delete_tree"sv)
                return fault_operation::delete_tree;
            if (s == u"rename_key"sv)
                return fault_operation::rename_key;
            if (s == u"set_value"sv)
                return fault_operation::set_value;
            if (s == u"delete_value"sv)
                return fault_operation::delete_value;
            if (s == u"get_value"sv)
                return fault_operation::get_value;

            if (s == u"create_directory"sv)
                return fault_operation::create_directory;
            if (s == u"create_file"sv)
                return fault_operation::create_file;
            if (s == u"open_directory"sv)
                return fault_operation::open_directory;
            if (s == u"open_file"sv)
                return fault_operation::open_file;
            if (s == u"remove_entry"sv)
                return fault_operation::remove_entry;
            if (s == u"delete_tree_entry"sv)
                return fault_operation::delete_tree_entry;
            if (s == u"rename_entry"sv)
                return fault_operation::rename_entry;

            if (s == u"webcore_activate"sv)
                return fault_operation::webcore_activate;

            throw m::invalid_parameter("fault script: unknown operation");
        }

        // True for the filesystem verbs (file_path domain); false for the
        // registry verbs (key_path domain). The parser uses this to decide how
        // to interpret a rule's path attribute.
        bool
        is_filesystem_operation(fault_operation op)
        {
            switch (op)
            {
            case fault_operation::create_directory:
            case fault_operation::create_file:
            case fault_operation::open_directory:
            case fault_operation::open_file:
            case fault_operation::remove_entry:
            case fault_operation::delete_tree_entry:
            case fault_operation::rename_entry:
                return true;
            default:
                return false;
            }
        }

        // True for the webcore verbs (instance name domain). The parser uses
        // this to decide how to interpret a rule's path attribute (which is an
        // instance name for webcore operations).
        bool
        is_webcore_operation(fault_operation op)
        {
            switch (op)
            {
            case fault_operation::webcore_activate:
                return true;
            default:
                return false;
            }
        }

        fault_action
        action_from_string(std::u16string_view s)
        {
            if (s == u"not_found"sv)
                return fault_action::not_found;
            if (s == u"access_denied"sv)
                return fault_action::access_denied;
            if (s == u"out_of_resources"sv)
                return fault_action::out_of_resources;
            if (s == u"sharing_violation"sv)
                return fault_action::sharing_violation;
            if (s == u"already_exists"sv)
                return fault_action::already_exists;
            if (s == u"not_supported"sv)
                return fault_action::not_supported;

            throw m::invalid_parameter("fault script: unknown action");
        }
    } // namespace

    fault_rule::fault_rule(fault_operation                            op,
                           key_path                                   target,
                           std::optional<pil::value_name_string_type> value_name,
                           std::uint64_t                              occurrence,
                           fault_action                               action):
        m_operation(op),
        m_target(target.native().view()),
        m_value_name(std::move(value_name)),
        m_occurrence(occurrence),
        m_action(action)
    {}

    fault_rule::fault_rule(fault_operation op,
                           file_path       target,
                           std::uint64_t   occurrence,
                           fault_action    action):
        m_operation(op),
        m_target(target.native().view()),
        m_value_name(std::nullopt),
        m_occurrence(occurrence),
        m_action(action)
    {}

    fault_rule::fault_rule(fault_operation     op,
                           std::u16string_view instance_name,
                           std::uint64_t       occurrence,
                           fault_action        action):
        m_operation(op),
        m_target(instance_name),
        m_value_name(std::nullopt),
        m_occurrence(occurrence),
        m_action(action)
    {}

    std::optional<fault_action>
    fault_rule::match_text_and_count(fault_operation                          op,
                                     std::u16string_view                      target_text,
                                     std::optional<pil::value_name_view_type> value_name)
    {
        if (op != m_operation)
            return std::nullopt;

        if (!ci_equal(target_text, m_target))
            return std::nullopt;

        // A rule that names a value constrains the match to that value; a rule
        // with no value name matches the operation regardless of value name.
        if (m_value_name.has_value())
        {
            if (!value_name.has_value() ||
                !ci_equal(value_name.value(), m_value_name.value().view()))
                return std::nullopt;
        }

        ++m_hits;

        // One-shot on the configured occurrence: fire on exactly the Nth match,
        // not before and not again afterward.
        if (m_hits == m_occurrence)
            return m_action;

        return std::nullopt;
    }

    std::optional<fault_action>
    fault_rule::match_and_count(fault_operation                          op,
                                key_path const&                          target,
                                std::optional<pil::value_name_view_type> value_name)
    {
        return match_text_and_count(op, target.native().view(), value_name);
    }

    std::optional<fault_action>
    fault_rule::match_and_count(fault_operation op, file_path const& target)
    {
        return match_text_and_count(op, target.native().view(), std::nullopt);
    }

    std::optional<fault_action>
    fault_rule::match_and_count(fault_operation op, std::u16string_view instance_name)
    {
        return match_text_and_count(op, instance_name, std::nullopt);
    }

    void
    fault_script::add_rule(fault_rule rule)
    {
        auto lock = std::unique_lock(m_mutex);
        m_rules.push_back(std::move(rule));
    }

    void
    fault_script::check(fault_operation                          op,
                        key_path const&                          target,
                        std::optional<pil::value_name_view_type> value_name)
    {
        auto lock = std::unique_lock(m_mutex);

        // Advance the counters of every matching rule before raising, so that
        // independent rules that happen to match the same operation each count
        // this occurrence consistently. The first rule that reaches its
        // threshold determines the raised action.
        std::optional<fault_action> fired;
        for (auto& rule: m_rules)
        {
            auto const action = rule.match_and_count(op, target, value_name);
            if (action.has_value() && !fired.has_value())
                fired = action;
        }

        if (fired.has_value())
            raise(fired.value());
    }

    void
    fault_script::check_filesystem(fault_operation op, file_path const& target)
    {
        auto lock = std::unique_lock(m_mutex);

        // Mirror check(): advance every matching rule's counter before raising,
        // so independent rules stay consistent, and the first rule to reach its
        // threshold determines the raised action.
        std::optional<fault_action> fired;
        for (auto& rule: m_rules)
        {
            auto const action = rule.match_and_count(op, target);
            if (action.has_value() && !fired.has_value())
                fired = action;
        }

        if (fired.has_value())
            raise(fired.value());
    }

    void
    fault_script::check_webcore(fault_operation op, std::u16string_view instance_name)
    {
        auto lock = std::unique_lock(m_mutex);

        // Mirror check(): advance every matching rule's counter before raising,
        // so independent rules stay consistent, and the first rule to reach its
        // threshold determines the raised action.
        std::optional<fault_action> fired;
        for (auto& rule: m_rules)
        {
            auto const action = rule.match_and_count(op, instance_name);
            if (action.has_value() && !fired.has_value())
                fired = action;
        }

        if (fired.has_value())
            raise(fired.value());
    }

    std::shared_ptr<fault_script>
    parse_fault_script(pugi::xml_node const& fault_script_node)
    {
        auto script = std::make_shared<fault_script>();

        for (auto rule_node = fault_script_node.child(M_PUGIXML_T("Rule"sv)); rule_node;
             rule_node      = rule_node.next_sibling(M_PUGIXML_T("Rule"sv)))
        {
            auto const op_attr = rule_node.attribute(M_PUGIXML_T("operation"sv));
            if (op_attr.empty())
                throw m::invalid_parameter("fault script: Rule missing operation");

            auto const path_attr = rule_node.attribute(M_PUGIXML_T("path"sv));
            if (path_attr.empty())
                throw m::invalid_parameter("fault script: Rule missing path");

            auto const action_attr = rule_node.attribute(M_PUGIXML_T("action"sv));
            if (action_attr.empty())
                throw m::invalid_parameter("fault script: Rule missing action");

            auto const occurrence_attr = rule_node.attribute(M_PUGIXML_T("occurrence"sv));
            if (occurrence_attr.empty())
                throw m::invalid_parameter("fault script: Rule missing occurrence");

            auto const occurrence = occurrence_attr.as_ullong(0);
            if (occurrence < 1)
                throw m::invalid_parameter("fault script: occurrence must be >= 1");

            auto const op     = operation_from_string(m::to_u16string(op_attr.as_string()));
            auto const action = action_from_string(m::to_u16string(action_attr.as_string()));

            // The operation selects the path domain: filesystem verbs target a
            // file_path (valueName has no meaning and is ignored), registry
            // verbs a key_path with an optional valueName constraint, webcore
            // verbs target an instance name string.
            if (is_filesystem_operation(op))
            {
                file_path target{file_path::view_type{m::to_u16string(path_attr.as_string())}};
                script->add_rule(fault_rule{op, std::move(target), occurrence, action});
                continue;
            }

            if (is_webcore_operation(op))
            {
                std::u16string instance_name{m::to_u16string(path_attr.as_string())};
                script->add_rule(fault_rule{op, instance_name, occurrence, action});
                continue;
            }

            key_path target{key_path::view_type{m::to_u16string(path_attr.as_string())}};

            std::optional<pil::value_name_string_type> value_name;
            auto const value_name_attr = rule_node.attribute(M_PUGIXML_T("valueName"sv));
            if (!value_name_attr.empty())
                value_name = pil::value_name_string_type{
                    m::to_u16string(value_name_attr.as_string())};

            script->add_rule(fault_rule{
                op, std::move(target), std::move(value_name), occurrence, action});
        }

        return script;
    }

} // namespace m::pil::impl::fault
