/*
 * ModSecurity, http://www.modsecurity.org/
 * Copyright (c) 2015 Trustwave Holdings, Inc. (http://www.trustwave.com/)
 *
 * You may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * If any of the files related to licensing are missing or if you have any
 * other questions related to licensing please contact Trustwave Holdings, Inc.
 * directly using the email address security@modsecurity.org.
 *
 */

#include "src/parser/driver.h"

#include "modsecurity/rules_set_properties.h"
#include "src/parser/seclang-parser.hh"
#include "modsecurity/audit_log.h"

using modsecurity::audit_log::AuditLog;
using modsecurity::Rule;

namespace modsecurity {
namespace Parser {

Driver::Driver()
  : RulesSetProperties(),
  trace_scanning(false),
  trace_parsing(false),
  m_lastRule(nullptr) { }


Driver::~Driver() {
    while (loc.empty() == false) {
        yy::location *a = loc.back();
        loc.pop_back();
        delete a;
    }
}


int Driver::addSecMarker(std::string marker) {
    for (int i = 0; i < modsecurity::Phases::NUMBER_OF_PHASES; i++) {
        std::unique_ptr<Rule> rule(new Rule(marker));
        rule->m_phase = i;
        m_rulesSetPhases.insert(std::move(rule));
    }
    return 0;
}


int Driver::addSecAction(std::unique_ptr<Rule> rule) {
    if (rule->m_phase >= modsecurity::Phases::NUMBER_OF_PHASES) {
        m_parserError << "Unknown phase: " << std::to_string(rule->m_phase);
        m_parserError << std::endl;
        return false;
    }


    m_rulesSetPhases.insert(std::move(rule));

    return true;
}


int Driver::addSecRuleScript(std::unique_ptr<RuleScript> rule) {
    m_rulesSetPhases.insert(std::move(rule));
    return true;
}


int Driver::addSecRule(std::unique_ptr<Rule> r) {
    if (r->m_phase >= modsecurity::Phases::NUMBER_OF_PHASES) {
        m_parserError << "Unknown phase: " << std::to_string(r->m_phase);
        m_parserError << std::endl;
        return false;
    }
    std::shared_ptr<Rule> rule(std::move(r));

    if (m_lastRule != nullptr && m_lastRule->m_chained) {
        rule->m_phase = m_lastRule->m_phase;
        if (rule->m_theDisruptiveAction) {
            m_parserError << "Disruptive actions can only be specified by";
            m_parserError << " chain starter rules.";
            return false;
        }
        m_lastRule->m_chainedRuleChild = rule;
        rule->m_chainedRuleParent = m_lastRule.get();
        m_lastRule = rule;
        return true;
    }

    /*
     * Checking if the rule has an ID and also checking if this ID is not used
     * by other rule
     */
    if (rule->m_ruleId == 0) {
        m_parserError << "Rules must have an ID. File: ";
        m_parserError << rule->m_fileName << " at line: ";
        m_parserError << std::to_string(rule->m_lineNumber) << std::endl;
        return false;
    }
    for (int i = 0; i < modsecurity::Phases::NUMBER_OF_PHASES; i++) {
        Rules *rules = m_rulesSetPhases[i];
        for (int j = 0; j < rules->size(); j++) {
            if (rules->at(j)->m_ruleId == rule->m_ruleId) {
                m_parserError << "Rule id: " << std::to_string(rule->m_ruleId) \
                    << " is duplicated" << std::endl;
                return false;
            }
        }
    }

    m_lastRule = rule;
    m_rulesSetPhases.insert(rule);

    return true;
}


int Driver::parse(const std::string &f, const std::string &ref) {
    m_lastRule = nullptr;
    loc.push_back(new yy::location());
    if (ref.empty()) {
        this->ref.push_back("<<reference missing or not informed>>");
    } else {
        this->ref.push_back(ref);
    }

    if (f.empty()) {
        return 1;
    }

    buffer = f;
    scan_begin();
    yy::seclang_parser parser(*this);
    parser.set_debug_level(trace_parsing);
    int res = parser.parse();
    scan_end();

    /*
    if (m_auditLog->init(&error) == false) {
        m_parserError << "Problems while initializing the audit logs: " \
            << error << std::endl;
        return false;
    }
    */

    return res == 0;
}


int Driver::parseFile(const std::string &f) {
    std::ifstream t(f);
    std::string str;

    if (utils::isFile(f) == false) {
        m_parserError << "Failed to open the file: " << f << std::endl;
        return false;
    }

    t.seekg(0, std::ios::end);
    str.reserve(t.tellg());
    t.seekg(0, std::ios::beg);

    str.assign((std::istreambuf_iterator<char>(t)),
        std::istreambuf_iterator<char>());

    return parse(str, f);
}


void Driver::error(const yy::location& l, const std::string& m) {
    error(l, m, "");
}


void Driver::error(const yy::location& l, const std::string& m,
    const std::string& c) {
    if (m_parserError.tellp() == 0) {
        m_parserError << "Rules error. ";
        if (ref.empty() == false) {
            m_parserError << "File: " << ref.back() << ". ";
        }
        m_parserError << "Line: " << l.end.line << ". ";
        m_parserError << "Column: " << l.end.column - 1 << ". ";
    }

    if (m.empty() == false) {
        m_parserError << "" << m << " ";
    }

    if (c.empty() == false) {
        m_parserError << c;
    }
}


}  // namespace Parser
}  // namespace modsecurity
