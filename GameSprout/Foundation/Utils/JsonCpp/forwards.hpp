// Copyright 2007-2010 Baptiste Lepilleur and The JsonCpp Authors
// Distributed under MIT license, or public domain if desired and
// recognized in your jurisdiction.
// See file LICENSE for detail or copy at http://jsoncpp.sourceforge.net/LICENSE

#ifndef JSON_FORWARDS_H_INCLUDED
#define JSON_FORWARDS_H_INCLUDED

#if !defined(JSON_IS_AMALGAMATION)
#include "config.hpp"
#endif // if !defined(JSON_IS_AMALGAMATION)

namespace Json {

// writer.hpp
class StreamWriter;
class StreamWriterBuilder;
class Writer;
class FastWriter;
class StyledWriter;
class StyledStreamWriter;

// reader.hpp
class Reader;
class CharReader;
class CharReaderBuilder;

// json_features.hpp
class Features;

// value.hpp
using ArrayIndex = unsigned int;
class StaticString;
class Path;
class PathArgument;
class Value;
class ValueIteratorBase;
class ValueIterator;
class ValueConstIterator;

} // namespace Json

#endif // JSON_FORWARDS_H_INCLUDED


