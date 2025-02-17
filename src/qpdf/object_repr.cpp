/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (C) 2017, James R. Barlow (https://github.com/jbarlow83/)
 */

/*
 * Implement repr() for QPDFObjectHandle
 *
 * Since qpdf largely ignores const, it is not possible to use const here,
 * even though repr() is const throughout.
 *
 * References are used for functions that are just passing handles around.
 * objecthandle_repr_inner cannot cannot use references because it calls itself.
 */

#include <sstream>
#include <iostream>
#include <iomanip>
#include <locale>

#include <qpdf/Constants.h>
#include <qpdf/Types.h>
#include <qpdf/DLL.h>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/PointerHolder.hh>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

#include "pikepdf.h"

std::string objecthandle_scalar_value(QPDFObjectHandle h)
{
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    switch (h.getTypeCode()) {
    case QPDFObject::object_type_e::ot_null:
        ss << "None";
        break;
    case QPDFObject::object_type_e::ot_boolean:
        ss << (h.getBoolValue() ? "True" : "False");
        break;
    case QPDFObject::object_type_e::ot_integer:
        ss << std::to_string(h.getIntValue());
        break;
    case QPDFObject::object_type_e::ot_real:
        ss << "Decimal('" + h.getRealValue() + "')";
        break;
    case QPDFObject::object_type_e::ot_name:
        ss << std::quoted(h.getName());
        break;
    case QPDFObject::object_type_e::ot_string:
        ss << std::quoted(h.getUTF8Value());
        break;
    case QPDFObject::object_type_e::ot_operator:
        ss << std::quoted(h.getOperatorValue());
        break;
    // LCOV_EXCL_START
    default:
        throw std::logic_error("object_handle_scalar value called for non-scalar");
        // LCOV_EXCL_STOP
    }
    return ss.str();
}

std::string objecthandle_pythonic_typename(QPDFObjectHandle h)
{
    std::ostringstream ss;
    ss.imbue(std::locale::classic());

    switch (h.getTypeCode()) {
    case QPDFObject::object_type_e::ot_name:
        ss << "pikepdf.Name";
        break;
    case QPDFObject::object_type_e::ot_string:
        ss << "pikepdf.String";
        break;
    case QPDFObject::object_type_e::ot_operator:
        ss << "pikepdf.Operator";
        break;
    // LCOV_EXCL_START
    case QPDFObject::object_type_e::ot_inlineimage:
        // Objects of this time are not directly returned.
        ss << "pikepdf.InlineImage";
        break;
    // LCOV_EXCL_STOP
    case QPDFObject::object_type_e::ot_array:
        ss << "pikepdf.Array";
        break;
    case QPDFObject::object_type_e::ot_dictionary:
        if (h.hasKey("/Type")) {
            ss << "pikepdf.Dictionary(Type=\"" << h.getKey("/Type").getName() << "\")";
        } else {
            ss << "pikepdf.Dictionary";
        }
        break;
    case QPDFObject::object_type_e::ot_stream:
        ss << "pikepdf.Stream";
        break;
    case QPDFObject::object_type_e::ot_null:
    case QPDFObject::object_type_e::ot_boolean:
    case QPDFObject::object_type_e::ot_integer:
    case QPDFObject::object_type_e::ot_real:
        break; // No typename since literal is obvious and Decimal automatically adds
               // Decimal('1.2345.')

    // LCOV_EXCL_START
    default:
        throw std::logic_error(
            std::string("Unexpected QPDF object type value: ") + h.getTypeName());
        // LCOV_EXCL_STOP
    }

    return ss.str();
}

std::string objecthandle_repr_typename_and_value(QPDFObjectHandle h)
{
    auto pythonic_typename = objecthandle_pythonic_typename(h);
    if (pythonic_typename.empty()) {
        return objecthandle_scalar_value(h);
    }
    return objecthandle_pythonic_typename(h) + "(" + objecthandle_scalar_value(h) + ")";
}

std::ostream &operator<<(std::ostream &os, const QPDFObjGen &og)
{
    os << og.getObj() << ", " << og.getGen();
    return os;
}

static std::string objecthandle_repr_inner(QPDFObjectHandle h,
    uint recursion_depth,
    uint indent_depth,
    std::set<QPDFObjGen> *visited,
    bool *pure_expr)
{
    StackGuard sg(" objecthandle_repr_inner");
    std::ostringstream ss;
    ss.imbue(std::locale::classic());

    if (!h.isScalar()) {
        if (visited->count(h.getObjGen()) > 0) {
            *pure_expr = false;
            ss << "<.get_object(" << h.getObjGen() << ")>";
            return ss.str();
        }

        if (!(h.getObjGen() == QPDFObjGen(0, 0)))
            visited->insert(h.getObjGen());
    }
    if (h.isPageObject() && recursion_depth >= 1 && h.isIndirect()) {
        ss << "<Pdf.pages.from_objgen"
           << "(" << h.getObjGen() << ")"
           << ">";
        return ss.str();
    }

    switch (h.getTypeCode()) {
    case QPDFObject::object_type_e::ot_null:
    case QPDFObject::object_type_e::ot_boolean:
    case QPDFObject::object_type_e::ot_integer:
    case QPDFObject::object_type_e::ot_real:
    case QPDFObject::object_type_e::ot_name:
    case QPDFObject::object_type_e::ot_string:
        ss << objecthandle_scalar_value(h);
        break;
    case QPDFObject::object_type_e::ot_operator:
        ss << objecthandle_repr_typename_and_value(h);
        break;
    case QPDFObject::object_type_e::ot_inlineimage:
        // LCOV_EXCL_START
        // Inline image objects are automatically promoted to higher level objects
        // in parse_content_stream, so objects of this type should not be returned
        // directly.
        ss << objecthandle_pythonic_typename(h) << "("
           << "data=<...>"
           << ")";
        break;
        // LCOV_EXCL_STOP
    case QPDFObject::object_type_e::ot_array:
        ss << "[";
        {
            bool first_item = true;
            ss << " ";
            for (auto item : h.getArrayAsVector()) {
                if (!first_item)
                    ss << ", ";
                first_item = false;
                // We don't increase indent_depth when recursing into arrays,
                // because it doesn't look right. Always increase recursion_depth.
                ss << objecthandle_repr_inner(
                    item, recursion_depth + 1, indent_depth, visited, pure_expr);
            }
            ss << " ";
        }
        ss << "]";
        break;
    case QPDFObject::object_type_e::ot_dictionary:
        ss << "{"; // This will end the line
        {
            bool first_item = true;
            ss << "\n";
            for (auto item : h.getDictAsMap()) {
                auto &key = item.first;
                auto &obj = item.second;
                if (!first_item)
                    ss << ",\n";
                first_item = false;
                ss << std::string((indent_depth + 1) * 2, ' '); // Indent each line
                if (key == "/Parent" && obj.isPagesObject()) {
                    // Don't visit /Parent keys since that just puts every page on the
                    // repr() of a single page
                    ss << std::quoted(key) << ": <reference to /Pages>";
                } else {
                    ss << std::quoted(key) << ": "
                       << objecthandle_repr_inner(obj,
                              recursion_depth + 1,
                              indent_depth + 1,
                              visited,
                              pure_expr);
                }
            }
            ss << "\n";
        }
        ss << std::string(indent_depth * 2, ' ') // Restore previous indent level
           << "}";
        break;
    case QPDFObject::object_type_e::ot_stream:
        *pure_expr = false;
        ss << objecthandle_pythonic_typename(h) << "(owner=<...>, data=<...>, "
           << objecthandle_repr_inner(h.getDict(),
                  recursion_depth + 1,
                  indent_depth + 1,
                  visited,
                  pure_expr)
           << ")";
        break;
    // LCOV_EXCL_START
    default:
        ss << "Unexpected QPDF object type value: " << h.getTypeCode();
        break;
        // LCOV_EXCL_STOP
    }

    return ss.str();
}

std::string objecthandle_repr(QPDFObjectHandle h)
{
    if (h.isScalar() || h.isOperator()) {
        // qpdf does not consider Operator a scalar but it is as far we
        // are concerned here
        return objecthandle_repr_typename_and_value(h);
    }

    std::set<QPDFObjGen> visited;
    bool pure_expr    = true;
    std::string inner = objecthandle_repr_inner(h, 0, 0, &visited, &pure_expr);
    std::string output;

    if (h.isScalar() || h.isDictionary() || h.isArray()) {
        output = objecthandle_pythonic_typename(h) + "(" + inner + ")";
    } else {
        output    = inner;
        pure_expr = false;
    }

    if (pure_expr) {
        // The output contains no external or parent objects so this object
        // can be output as a Python expression and rebuild with repr(output)
        return output;
    }
    // Output cannot be fully described in a Python expression
    return std::string("<") + output + ">";
}
