//===- FormatVariadic.h - Efficient type-safe string formatting --*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Copyed from llvm-project/llvm/include/Support/FormatVariadic.h
//
// This file implements the formatv() function which can be used with other LLVM
// subsystems to provide printf-like formatting, but with improved safety and
// flexibility.  The result of `formatv` is an object which can be streamed to
// a std::ostream or converted to a std::string.
//
//   // Convert to std::string.
//   std::string S = formatv("{0} {1}", 1234.412, "test").str();
//
//   // Stream to an existing std::ostream.
//   OS << formatv("{0} {1}", 1234.412, "test");
//
//===----------------------------------------------------------------------===//

#include <cmath>
#include <vector>
#include <cctype>
#include <cassert>
#include <sstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <type_traits>

//==------------------STL Extras-------------------==//

class StringRef : public std::string_view {

    static unsigned GetAutoSenseRadix(StringRef &Str) {
        if (Str.empty())
            return 10;

        if (Str.starts_with("0x") || Str.starts_with("0X")) {
            Str = Str.substr(2);
            return 16;
        }

        if (Str.starts_with("0b") || Str.starts_with("0B")) {
            Str = Str.substr(2);
            return 2;
        }

        if (Str.starts_with("0o")) {
            Str = Str.substr(2);
            return 8;
        }

        if (Str[0] == '0' && Str.size() > 1 && std::isdigit(Str[1])) {
            Str = Str.substr(1);
            return 8;
        }

        return 10;
    }

    bool getAsUnsignedInteger(StringRef Str, unsigned Radix,
                              unsigned long long &Result) const {
        if (consumeUnsignedInteger(Str, Radix, Result))
            return true;

        // For getAsUnsignedInteger, we require the whole string to be consumed
        // or else we consider it a failure.
        return !Str.empty();
    }

    bool getAsSignedInteger(StringRef Str, unsigned Radix,
                            long long &Result) const {
        if (consumeSignedInteger(Str, Radix, Result))
            return true;

        // For getAsSignedInteger, we require the whole string to be consumed or
        // else we consider it a failure.
        return !Str.empty();
    }

    bool consumeUnsignedInteger(StringRef &Str, unsigned Radix,
                                unsigned long long &Result) const {
        // Autosense radix if not specified.
        if (Radix == 0)
            Radix = GetAutoSenseRadix(Str);

        // Empty strings (after the radix autosense) are invalid.
        if (Str.empty())
            return true;

        // Parse all the bytes of the string given this radix.  Watch for
        // overflow.
        StringRef Str2 = Str;
        Result = 0;
        while (!Str2.empty()) {
            unsigned CharVal;
            if (Str2[0] >= '0' && Str2[0] <= '9')
                CharVal = Str2[0] - '0';
            else if (Str2[0] >= 'a' && Str2[0] <= 'z')
                CharVal = Str2[0] - 'a' + 10;
            else if (Str2[0] >= 'A' && Str2[0] <= 'Z')
                CharVal = Str2[0] - 'A' + 10;
            else
                break;

            // If the parsed value is larger than the integer radix, we cannot
            // consume any more characters.
            if (CharVal >= Radix)
                break;

            // Add in this character.
            unsigned long long PrevResult = Result;
            Result = Result * Radix + CharVal;

            // Check for overflow by shifting back and seeing if bits were lost.
            if (Result / Radix < PrevResult)
                return true;

            Str2 = Str2.substr(1);
        }

        // We consider the operation a failure if no characters were consumed
        // successfully.
        if (Str.size() == Str2.size())
            return true;

        Str = Str2;
        return false;
    }
    bool consumeSignedInteger(StringRef &Str, unsigned Radix,
                              long long &Result) const {
        unsigned long long ULLVal;

        // Handle positive strings first.
        if (Str.empty() || Str.front() != '-') {
            if (consumeUnsignedInteger(Str, Radix, ULLVal) ||
                // Check for value so large it overflows a signed value.
                (long long)ULLVal < 0)
                return true;
            Result = ULLVal;
            return false;
        }

        // Get the positive part of the value.
        StringRef Str2 = Str.drop_front(1);
        if (consumeUnsignedInteger(Str2, Radix, ULLVal) ||
            // Reject values so large they'd overflow as negative signed, but
            // allow
            // "-0".  This negates the unsigned so that the negative isn't
            // undefined on signed overflow.
            (long long)-ULLVal > 0)
            return true;

        Str = Str2;
        Result = -ULLVal;
        return false;
    }

  public:
    StringRef() : std::string_view() {}
    StringRef(const StringRef &) = default;
    StringRef &operator=(const StringRef &) = default;

    StringRef(const char *str, size_t len) : std::string_view(str, len) {}

    StringRef(const char *str) : std::string_view(str) {}

#if __cplusplus <= 201703L
    bool starts_with(StringRef str) const {
        return size() >= str.size() && compare(0, str.size(), str) == 0;
    }

    bool starts_with(char c) const {
        return !empty() && std::char_traits<char>::eq(front(), c);
    }

    bool starts_with(const char *str) const {
        return starts_with(StringRef(str));
    }

    bool ends_with(StringRef str) const {
        return size() >= str.size() &&
               compare(size() - str.size(), npos, str) == 0;
    }

    bool ends_with(char c) const {
        return !empty() && std::char_traits<char>::eq(back(), c);
    }

    bool ends_with(const char *str) const {
        return ends_with(StringRef(str));
    }
#endif

    bool startswith_lower(StringRef Prefix) const {
        return size() >= Prefix.size() &&
               strncasecmp(data(), Prefix.data(), Prefix.size()) == 0;
    }

    bool endswith_lower(StringRef Suffix) const {
        return size() >= Suffix.size() &&
               strncasecmp(end() - Suffix.size(), Suffix.data(),
                           Suffix.size()) == 0;
    }

    StringRef substr(size_t Start, size_t N = npos) const {
        Start = std::min(Start, size());
        return StringRef(data() + Start, std::min(N, size() - Start));
    }

    StringRef take_front(size_t N = 1) const {
        if (N >= size())
            return *this;
        return drop_back(size() - N);
    }

    StringRef take_back(size_t N = 1) const {
        if (N >= size())
            return *this;
        return drop_front(size() - N);
    }

    StringRef drop_front(size_t N = 1) const {
        assert(size() >= N && "Dropping more elements than exist");
        return substr(N);
    }

    StringRef drop_back(size_t N = 1) const {
        assert(size() >= N && "Dropping more elements than exist");
        return substr(0, size() - N);
    }

    bool consume_front(StringRef Prefix) {
        if (!starts_with(Prefix))
            return false;

        *this = drop_front(Prefix.size());
        return true;
    }

    bool consume_back(StringRef Suffix) {
        if (!ends_with(Suffix))
            return false;

        *this = drop_back(Suffix.size());
        return true;
    }

    /// Parse the current string as an integer of the specified radix.  If
    /// \p Radix is specified as zero, this does radix autosensing using
    /// extended C rules: 0 is octal, 0x is hex, 0b is binary.
    ///
    /// If the string is invalid or if only a subset of the string is valid,
    /// this returns true to signify the error.  The string is considered
    /// erroneous if empty or if it overflows T.
    template <typename T>
    typename std::enable_if<std::numeric_limits<T>::is_signed, bool>::type
    getAsInteger(unsigned Radix, T &Result) const {
        long long LLVal;
        if (getAsSignedInteger(*this, Radix, LLVal) ||
            static_cast<T>(LLVal) != LLVal)
            return true;
        Result = LLVal;
        return false;
    }

    template <typename T>
    typename std::enable_if<!std::numeric_limits<T>::is_signed, bool>::type
    getAsInteger(unsigned Radix, T &Result) const {
        unsigned long long ULLVal;
        // The additional cast to unsigned long long is required to avoid the
        // Visual C++ warning C4805: '!=' : unsafe mix of type 'bool' and type
        // 'unsigned __int64' when instantiating getAsInteger with T = bool.
        if (getAsUnsignedInteger(*this, Radix, ULLVal) ||
            static_cast<unsigned long long>(static_cast<T>(ULLVal)) != ULLVal)
            return true;
        Result = ULLVal;
        return false;
    }

    /// Parse the current string as an integer of the specified radix.  If
    /// \p Radix is specified as zero, this does radix autosensing using
    /// extended C rules: 0 is octal, 0x is hex, 0b is binary.
    ///
    /// If the string does not begin with a number of the specified radix,
    /// this returns true to signify the error. The string is considered
    /// erroneous if empty or if it overflows T.
    /// The portion of the string representing the discovered numeric value
    /// is removed from the beginning of the string.
    template <typename T>
    typename std::enable_if<std::numeric_limits<T>::is_signed, bool>::type
    consumeInteger(unsigned Radix, T &Result) {
        long long LLVal;
        if (consumeSignedInteger(*this, Radix, LLVal) ||
            static_cast<long long>(static_cast<T>(LLVal)) != LLVal)
            return true;
        Result = LLVal;
        return false;
    }

    template <typename T>
    typename std::enable_if<!std::numeric_limits<T>::is_signed, bool>::type
    consumeInteger(unsigned Radix, T &Result) {
        unsigned long long ULLVal;
        if (consumeUnsignedInteger(*this, Radix, ULLVal) ||
            static_cast<unsigned long long>(static_cast<T>(ULLVal)) != ULLVal)
            return true;
        Result = ULLVal;
        return false;
    }

    /// Return string with consecutive \p Char characters starting from the
    /// the left removed.
    StringRef ltrim(char Char) const {
        return drop_front(std::min(size(), find_first_not_of(Char)));
    }

    /// Return string with consecutive characters in \p Chars starting from
    /// the left removed.
    StringRef ltrim(StringRef Chars = " \t\n\v\f\r") const {
        return drop_front(std::min(size(), find_first_not_of(Chars)));
    }

    /// Return string with consecutive \p Char characters starting from the
    /// right removed.
    StringRef rtrim(char Char) const {
        return drop_back(size() - std::min(size(), find_last_not_of(Char) + 1));
    }

    /// Return string with consecutive characters in \p Chars starting from
    /// the right removed.
    StringRef rtrim(StringRef Chars = " \t\n\v\f\r") const {
        return drop_back(size() -
                         std::min(size(), find_last_not_of(Chars) + 1));
    }

    /// Return string with consecutive \p Char characters starting from the
    /// left and right removed.
    StringRef trim(char Char) const {
        return ltrim(Char).rtrim(Char);
    }

    /// Return string with consecutive characters in \p Chars starting from
    /// the left and right removed.
    StringRef trim(StringRef Chars = " \t\n\v\f\r") const {
        return ltrim(Chars).rtrim(Chars);
    }

    StringRef slice(size_t Start, size_t End) const {
        Start = std::min(Start, size());
        End = std::min(std::max(Start, End), size());
        return StringRef(data() + Start, End - Start);
    }

    size_t find_if(std::function<bool(char)> F, size_t From = 0) const {
        StringRef S = drop_front(From);
        while (!S.empty()) {
            if (F(S.front()))
                return size() - S.size();
            S = S.drop_front();
        }
        return npos;
    }

    size_t find_if_not(std::function<bool(char)> F, size_t From = 0) const {
        return find_if([F](char c) { return !F(c); }, From);
    }

    StringRef take_while(std::function<bool(char)> F) const {
        return substr(0, find_if_not(F));
    }

    StringRef take_until(std::function<bool(char)> F) const {
        return substr(0, find_if(F));
    }
};

template <typename T, std::size_t SizeOfT> struct LeadingZerosCounter {
    static std::size_t count(T Val) {
        if (!Val)
            return std::numeric_limits<T>::digits;

        // Bisection method.
        std::size_t ZeroBits = 0;
        for (T Shift = std::numeric_limits<T>::digits >> 1; Shift;
             Shift >>= 1) {
            T Tmp = Val >> Shift;
            if (Tmp)
                Val = Tmp;
            else
                ZeroBits |= Shift;
        }
        return ZeroBits;
    }
};

#if __GNUC__ >= 4 || defined(_MSC_VER)
template <typename T> struct LeadingZerosCounter<T, 4> {
    static std::size_t count(T Val) {
        if (Val == 0)
            return 32;

#if __has_builtin(__builtin_clz)
        return __builtin_clz(Val);
#elif defined(_MSC_VER)
        unsigned long Index;
        _BitScanReverse(&Index, Val);
        return Index ^ 31;
#endif
    }
};

#if !defined(_MSC_VER) || defined(_M_X64)
template <typename T> struct LeadingZerosCounter<T, 8> {
    static std::size_t count(T Val) {
        if (Val == 0)
            return 64;

#if __has_builtin(__builtin_clzll)
        return __builtin_clzll(Val);
#elif defined(_MSC_VER)
        unsigned long Index;
        _BitScanReverse64(&Index, Val);
        return Index ^ 63;
#endif
    }
};
#endif
#endif

template <typename T> std::size_t countLeadingZeros(T Val) {
    static_assert(std::numeric_limits<T>::is_integer &&
                      !std::numeric_limits<T>::is_signed,
                  "Only unsigned integral types are allowed.");
    return LeadingZerosCounter<T, sizeof(T)>::count(Val);
}

inline char hexdigit(unsigned X, bool LowerCase = false) {
    const char HexChar = LowerCase ? 'a' : 'A';
    return X < 10 ? '0' + X : HexChar + X - 10;
}

template <typename T, typename... Ts> struct is_one_of {
    static const bool value = false;
};

template <typename T, typename U, typename... Ts>
struct is_one_of<T, U, Ts...> {
    static const bool value =
        std::is_same_v<T, U> || is_one_of<T, Ts...>::value;
};

template <class T, T... I> struct integer_sequence {
    using value_type = T;

    static constexpr size_t size() {
        return sizeof...(I);
    }
};

/// Alias for the common case of a sequence of size_ts.
template <size_t... I>
struct index_sequence : integer_sequence<std::size_t, I...> {};

template <std::size_t N, std::size_t... I>
struct build_index_impl : build_index_impl<N - 1, N - 1, I...> {};
template <std::size_t... I>
struct build_index_impl<0, I...> : index_sequence<I...> {};

namespace detail {

template <typename F, typename Tuple, std::size_t... I>
auto apply_tuple_impl(F &&f, Tuple &&t, index_sequence<I...>)
    -> decltype(std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...)) {
    return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}

} // end namespace detail

/// Given an input tuple (a1, a2, ..., an), pass the arguments of the
/// tuple variadically to f as if by calling f(a1, a2, ..., an) and
/// return the result.
template <typename F, typename Tuple>
auto apply_tuple(F &&f, Tuple &&t) -> decltype(detail::apply_tuple_impl(
    std::forward<F>(f), std::forward<Tuple>(t),
    build_index_impl<
        std::tuple_size<typename std::decay<Tuple>::type>::value>{})) {
    using Indices = build_index_impl<
        std::tuple_size<typename std::decay<Tuple>::type>::value>;

    return detail::apply_tuple_impl(std::forward<F>(f), std::forward<Tuple>(t),
                                    Indices{});
}

//==-----------Formart Variadic Details------------==//

template <typename T, typename Enable = void> struct format_provider {};

namespace detail {
class format_adapter {
  protected:
    virtual ~format_adapter() {}

  public:
    virtual void format(std::ostream &os, StringRef options) = 0;
};

template <typename T> class provider_format_adapter : public format_adapter {
    T item;

  public:
    explicit provider_format_adapter(T &&item) : item(std::forward<T>(item)) {}
    void format(std::ostream &os, StringRef options) override {
        format_provider<std::decay_t<T>>::format(item, os, options);
    }
};

template <typename T>
class stream_operator_format_adapter : public format_adapter {
    T item;

  public:
    explicit stream_operator_format_adapter(T &&item)
        : item(std::forward<T>(item)) {}
    void format(std::ostream &os, StringRef options) override {
        os << item;
    }
};

template <typename T> class missing_format_adapter;

template <typename T> struct has_format_provider {
    using Decayed = std::decay_t<T>;
    using SignatureFormat = void (*)(const Decayed &, std::ostream &os,
                                     StringRef);

    template <typename U>
    static char test(
        std::enable_if_t<std::is_same_v<SignatureFormat, &U::format>, int *>);

    template <typename U> static double test(...);

    static const bool value = (sizeof(test<Decayed>(nullptr)) == 1);
};

template <typename T> struct has_ostream_operator {
    using ConstRef = const std::decay_t<T> &;

    template <typename U>
    static char test(
        std::enable_if_t<std::is_same_v<decltype(std::declval<std::ostream &>()
                                                 << std::declval<U>()),
                                        std::ostream &>,
                         int *>);

    template <typename U> static double test(...);

    static const bool value = (sizeof(test<ConstRef>(nullptr)) == 1);
};

template <typename T>
struct uses_format_member
    : public std::integral_constant<
          bool, std::is_base_of_v<format_adapter, std::remove_reference_t<T>>> {
};

template <typename T>
struct uses_format_provider
    : public std::integral_constant<bool, !uses_format_member<T>::value &&
                                              has_format_provider<T>::value> {};

template <typename T>
struct uses_stream_operator
    : public std::integral_constant<bool, !uses_format_member<T>::value &&
                                              !uses_format_provider<T>::value &&
                                              has_ostream_operator<T>::value> {
};

template <typename T>
struct uses_missing_provider
    : public std::integral_constant<bool, !uses_format_member<T>::value &&
                                              !uses_format_provider<T>::value &&
                                              has_format_provider<T>::value> {};

template <typename T>
std::enable_if_t<uses_format_member<T>::value, T>
build_format_adapter(T &&item) {
    return std::forward<T>(item);
}

template <typename T>
std::enable_if_t<uses_format_member<T>::value, provider_format_adapter<T>>
build_format_adapter(T &&item) {
    return provider_format_adapter<T>(std::forward<T>(item));
}

template <typename T>
std::enable_if_t<uses_stream_operator<T>::value,
                 stream_operator_format_adapter<T>>
build_format_adapter(T &&item) {
    return stream_operator_format_adapter<T>(std::forward<T>(item));
}

template <typename T>
std::enable_if_t<uses_missing_provider<T>::value, missing_format_adapter<T>>
build_format_adaptor(T &&item) {
    return missing_format_adapter<T>();
}
} // end namespace detail

//==-----------Formart Variadic Common------------==//
enum class AlignStyle { Left, Center, Right };

struct FmtAlign {
    detail::format_adapter &adaptor;
    AlignStyle where;
    size_t amount;
    char fill_char;

    FmtAlign(detail::format_adapter &adaptor, AlignStyle where, size_t amount,
             char fill = ' ')
        : adaptor(adaptor), where(where), amount(amount), fill_char(fill) {}

    void format(std::ostream &os, StringRef options) {
        if (amount == 0) {
            adaptor.format(os, options);
            return;
        }

        std::stringstream stream;
        adaptor.format(stream, options);

        std::string item = stream.str();
        if (amount < item.size()) {
            os << item;
            return;
        }

        size_t pad_amount = amount - item.size();
        switch (where) {
        case AlignStyle::Left:
            os << item;
            fill(os, pad_amount);
            break;
        case AlignStyle::Center: {
            size_t tmp = pad_amount / 2;
            fill(os, tmp);
            os << item;
            fill(os, tmp);
            break;
        }
        case AlignStyle::Right:
            fill(os, pad_amount);
            os << item;
            break;
        }
    }

  protected:
    void fill(std::ostream &os, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i)
            os << fill_char;
    }
};

//==-----------Formart Variadic Adaptor------------==//
template <typename T> class FormatAdapter : public detail::format_adapter {
  protected:
    explicit FormatAdapter(T &&item) : item(std::forward<T>(item)) {}
    T item;
};

namespace detail {
template <typename T> class AlignAdapter final : public FormatAdapter<T> {
    AlignStyle where;
    size_t amount;
    char fill_char;

  public:
    AlignAdapter(T &&item, AlignStyle where, size_t amount, char fill_char)
        : FormatAdapter<T>(std::forward<T>(item)), where(where), amount(amount),
          fill_char(fill_char) {}

    void format(std::ostream &os, StringRef options) {
        auto adaptor =
            detail::build_format_adapter(std::forward<T>(this->item));
        FmtAlign(adaptor, where, amount, fill_char).format(os, options);
    }
};

template <typename T> class PadAdapter final : public FormatAdapter<T> {
    size_t left;
    size_t right;

  public:
    PadAdapter(T &&item, size_t left, size_t right)
        : FormatAdapter<T>(std::forward(item)), left(left), right(right) {}
    void format(std::ostream &os, StringRef options) {
        auto adaptor =
            detail::build_format_adapter(std::forward<T>(this->item));
        auto fill = [&os](int count) {
            for (size_t i = 0; i < count; ++i)
                os << ' ';
        };
        fill(left);
        adaptor.format(os, options);
        fill(right);
    }
};

template <typename T> class RepeatAdapter final : public FormatAdapter<T> {
    size_t count;

  public:
    RepeatAdapter(T &&item, size_t count)
        : FormatAdapter<T>(std::forward<T>(item)), count(count) {}

    void format(std::ostream &os, StringRef options) {
        auto adaptor =
            detail::build_format_adapter(std::forward<T>(this->item));
        for (size_t i = 0; i < count; ++i)
            adaptor.format(os, options);
    }
};
} // end namespace detail

template <typename T>
detail::AlignAdapter<T> fmt_align(T &&item, AlignStyle where, size_t amount,
                                  char fill_char = ' ') {
    return detail::AlignAdapter<T>(std::forward<T>(item), where, amount,
                                   fill_char);
}

template <typename T>
detail::PadAdapter<T> fmt_pad(T &&item, size_t left, size_t right) {
    return detail::PadAdapter<T>(std::forward<T>(item), left, right);
}

template <typename T>
detail::RepeatAdapter<T> fmt_repeat(T &&item, size_t count) {
    return detail::RepeatAdapter<T>(std::forward<T>(item), count);
}

//==-----------Formart Variadic Provider------------==//

enum class IntegerStyle { Integer, Number };
enum class FloatStyle { Exponent, ExponentUpper, Fixed, Percent };
enum class HexPrintStyle { Upper, Lower, PrefixUpper, PrefixLower };

template <typename T, std::size_t N>
int format_to_buffer(T value, char (&buffer)[N]) {
    char *end = std::end(buffer);
    char *cur = end;

    do {
        *--cur = '0' + char(value % 10);
        value /= 10;
    } while (value);
    return end - cur;
}

void write_with_commas(std::ostream &os, std::string buffer) {
    assert(buffer.empty());
    std::string this_group;
    int init_digits = ((buffer.size() - 1) % 3) + 1;
    this_group = buffer.substr(0, init_digits);
    os << this_group;
    buffer = buffer.substr(init_digits);
    assert(buffer.size() % 3 == 0);
    while (!buffer.empty()) {
        os << ',';
        this_group = buffer.substr(0, 3);
        os << this_group;
        buffer = buffer.substr(3);
    }
}

template <typename T>
void write_unsigned_impl(std::ostream &os, T n, size_t min_digits,
                         IntegerStyle style, bool is_negetive) {
    static_assert(std::is_unsigned_v<T>, "value is not unsigned");

    char number_buffer[128];
    std::memset(number_buffer, '0', sizeof(number_buffer));

    size_t len = 0;
    len = format_to_buffer(n, number_buffer);

    if (is_negetive)
        os << '-';

    if (len < min_digits && style != IntegerStyle::Number) {
        for (size_t i = len; i < min_digits; ++i)
            os << '0';
    }

    if (style == IntegerStyle::Number) {
        write_with_commas(os, std::string(std::end(number_buffer) - len, len));
    } else {
        os.write(std::end(number_buffer) - len, len);
    }
}

template <typename T>
void write_unsigned(std::ostream &os, T n, size_t min_digits,
                    IntegerStyle style, bool is_negtive = false) {
    if (n == static_cast<uint32_t>(n))
        write_unsigned_impl(os, static_cast<uint32_t>(n), min_digits, style,
                            is_negtive);
    else
        write_unsigned_impl(os, n, min_digits, style, is_negtive);
}

template <typename T>
void write_signed(std::ostream &os, T n, size_t min_digits,
                  IntegerStyle style) {
    static_assert(std::is_signed_v<T>, "value is not signed");
    using UnsignedTy = std::make_unsigned_t<T>;

    if (n >= 0) {
        write_unsigned(os, static_cast<UnsignedTy>(n), min_digits, style);
    } else {
        UnsignedTy u = -(UnsignedTy)n;
        write_unsigned(os, u, min_digits, style, true);
    }
}

size_t get_default_precision(FloatStyle style) {
    switch (style) {
    case FloatStyle::Exponent:
    case FloatStyle::ExponentUpper:
        return 6;
    case FloatStyle::Fixed:
    case FloatStyle::Percent:
        return 2;
    }
}

bool is_prefixed_hex_style(HexPrintStyle style) {
    return (style == HexPrintStyle::PrefixLower ||
            style == HexPrintStyle::PrefixUpper);
}

void write_integer(std::ostream &os, unsigned int n, size_t min_digits,
                   IntegerStyle style) {
    write_unsigned(os, n, min_digits, style);
}

void write_integer(std::ostream &os, int n, size_t min_digits,
                   IntegerStyle style) {
    write_signed(os, n, min_digits, style);
}

void write_integer(std::ostream &os, unsigned long n, size_t min_digits,
                   IntegerStyle style) {
    write_unsigned(os, n, min_digits, style);
}

void write_integer(std::ostream &os, long n, size_t min_digits,
                   IntegerStyle style) {
    write_signed(os, n, min_digits, style);
}

void write_integer(std::ostream &os, unsigned long long n, size_t min_digits,
                   IntegerStyle style) {
    write_unsigned(os, n, min_digits, style);
}

void write_integer(std::ostream &os, long long n, size_t min_digits,
                   IntegerStyle style) {
    write_signed(os, n, min_digits, style);
}

void write_hex(std::ostream &os, uint64_t n, HexPrintStyle style,
               std::optional<size_t> width = std::nullopt) {
    const size_t MAX_WIDTH = 128;

    size_t w = std::min(MAX_WIDTH, width.value_or(0U));
    unsigned nibbles = (64 - countLeadingZeros(n) + 3) / 4;
    bool prefix = (style == HexPrintStyle::PrefixLower ||
                   style == HexPrintStyle::PrefixUpper);
    bool upper =
        (style == HexPrintStyle::Upper || style == HexPrintStyle::PrefixUpper);
    unsigned prefix_chars = prefix ? 2 : 0;
    unsigned num_chars = std::max(static_cast<unsigned>(w),
                                  std::max(1U, nibbles) + prefix_chars);
    char number_buffer[MAX_WIDTH];
    std::memset(number_buffer, '0', sizeof(number_buffer));
    if (prefix)
        number_buffer[1] = 'x';
    char *end = number_buffer + num_chars;
    char *cur = end;
    while (n) {
        unsigned char x = static_cast<unsigned char>(n) % 16;
        *--cur = hexdigit(x, !upper);
        n /= 16;
    }
    os.write(number_buffer, num_chars);
}

void write_double(std::ostream &os, double d, FloatStyle style,
                  std::optional<size_t> precision = std::nullopt) {
    size_t prec = precision.value_or(get_default_precision(style));

    if (std::isnan(d)) {
        os << "nan";
    } else if (std::isinf(d)) {
        os << "INF";
        return;
    }

    char letter;
    if (style == FloatStyle::Exponent)
        letter = 'e';
    else if (style == FloatStyle::ExponentUpper)
        letter = 'E';
    else
        letter = 'f';

    std::stringstream out;
    out << "%." << prec << letter;
    std::string spec = out.str();
    if (style == FloatStyle::Exponent || style == FloatStyle::ExponentUpper) {
#ifdef _WIN32
// On MSVCRT and compatible, output of %e is incompatible to Posix
// by default. Number of exponent digits should be at least 2. "%+03d"
// FIXME: Implement our formatter to here or Support/Format.h!
#if defined(__MINGW32__)
        // FIXME: It should be generic to C++11.
        if (d == 0.0 && std::signbit(d)) {
            char NegativeZero[] = "-0.000000e+00";
            if (style == FloatStyle::ExponentUpper)
                NegativeZero[strlen(NegativeZero) - 4] = 'E';
            os << NegativeZero;
            return;
        }
#else
        int fpcl = _fpclass(d);
        // negative zero
        if (fpcl == _FPCLASS_NZ) {
            char NegativeZero[] = "-0.000000e+00";
            if (Style == FloatStyle::ExponentUpper)
                NegativeZero[strlen(NegativeZero) - 4] = 'E';
            os << NegativeZero;
            return;
        }
#endif

        char buf[32];
        unsigned len;
        len = snprintf(buf, sizeof(buf), spec.c_str(), d);
        if (len <= sizeof(buf) - 2) {
            if (len >= 5 && (buf[len - 5] == 'e' || buf[len - 5] == 'E') &&
                buf[len - 3] == '0') {
                int cs = buf[len - 4];
                if (cs == '+' || cs == '-') {
                    int c1 = buf[len - 2];
                    int c0 = buf[len - 1];
                    if (isdigit(static_cast<unsigned char>(c1)) &&
                        isdigit(static_cast<unsigned char>(c0))) {
                        // Trim leading '0': "...e+012" -> "...e+12\0"
                        buf[len - 3] = c1;
                        buf[len - 2] = c0;
                        buf[--len] = 0;
                    }
                }
            }
            os << buf;
            return;
        }
#endif
    }

    if (style == FloatStyle::Percent)
        d *= 100.0;

    char buf[32];
    snprintf(buf, sizeof(buf), spec.c_str(), d);
    os << buf;
    if (style == FloatStyle::Percent)
        os << '%';
}

namespace detail {
template <typename T>
struct use_integral_formatter
    : public std::integral_constant<
          bool, is_one_of<T, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                          int64_t, uint64_t, int, unsigned, long, unsigned long,
                          long long, unsigned long long>::value> {};

template <typename T>
struct use_char_formatter
    : public std::integral_constant<bool, std::is_same<T, char>::value> {};

template <typename T>
struct is_cstring
    : public std::integral_constant<bool,
                                    is_one_of<T, char *, const char *>::value> {
};

template <typename T>
struct use_string_formatter
    : public std::integral_constant<bool,
                                    std::is_convertible<T, StringRef>::value> {
};

template <typename T>
struct use_pointer_formatter
    : public std::integral_constant<bool, std::is_pointer<T>::value &&
                                              !is_cstring<T>::value> {};

template <typename T>
struct use_double_formatter
    : public std::integral_constant<bool, std::is_floating_point<T>::value> {};

class HelperFunctions {
  protected:
    static std::optional<size_t> parse_numeric_precision(StringRef Str) {
        size_t Prec;
        std::optional<size_t> Result;
        if (Str.empty())
            Result = std::nullopt;
        else if (Str.getAsInteger(10, Prec)) {
            assert(false && "Invalid precision specifier");
            Result = std::nullopt;
        } else {
            assert(Prec < 100 && "Precision out of range");
            Result = std::min<size_t>(99u, Prec);
        }
        return Result;
    }

    static bool consume_hex_style(StringRef &Str, HexPrintStyle &Style) {
        if (!Str.startswith_lower("x"))
            return false;

        if (Str.consume_front("x-"))
            Style = HexPrintStyle::Lower;
        else if (Str.consume_front("X-"))
            Style = HexPrintStyle::Upper;
        else if (Str.consume_front("x+") || Str.consume_front("x"))
            Style = HexPrintStyle::PrefixLower;
        else if (Str.consume_front("X+") || Str.consume_front("X"))
            Style = HexPrintStyle::PrefixUpper;
        return true;
    }

    static size_t consume_num_hex_digits(StringRef &Str, HexPrintStyle Style,
                                         size_t Default) {
        Str.consumeInteger(10, Default);
        if (is_prefixed_hex_style(Style))
            Default += 2;
        return Default;
    }
};
} // end namespace detail

/// Implementation of format_provider<T> for integral arithmetic types.
///
/// The options string of an integral type has the grammar:
///
///   integer_options   :: [style][digits]
///   style             :: <see table below>
///   digits            :: <non-negative integer> 0-99
///
///   ==========================================================================
///   |  style  |     Meaning          |      Example     | Digits Meaning     |
///   --------------------------------------------------------------------------
///   |         |                      |  Input |  Output |                    |
///   ==========================================================================
///   |   x-    | Hex no prefix, lower |   42   |    2a   | Minimum # digits   |
///   |   X-    | Hex no prefix, upper |   42   |    2A   | Minimum # digits   |
///   | x+ / x  | Hex + prefix, lower  |   42   |   0x2a  | Minimum # digits   |
///   | X+ / X  | Hex + prefix, upper  |   42   |   0x2A  | Minimum # digits   |
///   | N / n   | Digit grouped number | 123456 | 123,456 | Ignored            |
///   | D / d   | Integer              | 100000 | 100000  | Ignored            |
///   | (empty) | Same as D / d        |        |         |                    |
///   ==========================================================================
///

template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_integral_formatter<T>::value>::type>
    : public detail::HelperFunctions {
  private:
  public:
    static void format(const T &V, std::ostream &Stream, StringRef Style) {
        HexPrintStyle HS;
        size_t Digits = 0;
        if (consume_hex_style(Style, HS)) {
            Digits = consume_num_hex_digits(Style, HS, 0);
            write_hex(Stream, V, HS, Digits);
            return;
        }

        IntegerStyle IS = IntegerStyle::Integer;
        if (Style.consume_front("N") || Style.consume_front("n"))
            IS = IntegerStyle::Number;
        else if (Style.consume_front("D") || Style.consume_front("d"))
            IS = IntegerStyle::Integer;

        Style.consumeInteger(10, Digits);
        assert(Style.empty() && "Invalid integral format style!");
        write_integer(Stream, V, Digits, IS);
    }
};

/// Implementation of format_provider<T> for integral pointer types.
///
/// The options string of a pointer type has the grammar:
///
///   pointer_options   :: [style][precision]
///   style             :: <see table below>
///   digits            :: <non-negative integer> 0-sizeof(void*)
///
///   ==========================================================================
///   |   S     |     Meaning          |                Example                |
///   --------------------------------------------------------------------------
///   |         |                      |       Input       |      Output       |
///   ==========================================================================
///   |   x-    | Hex no prefix, lower |    0xDEADBEEF     |     deadbeef      |
///   |   X-    | Hex no prefix, upper |    0xDEADBEEF     |     DEADBEEF      |
///   | x+ / x  | Hex + prefix, lower  |    0xDEADBEEF     |    0xdeadbeef     |
///   | X+ / X  | Hex + prefix, upper  |    0xDEADBEEF     |    0xDEADBEEF     |
///   | (empty) | Same as X+ / X       |                   |                   |
///   ==========================================================================
///
/// The default precision is the number of nibbles in a machine word, and in all
/// cases indicates the minimum number of nibbles to print.
template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_pointer_formatter<T>::value>::type>
    : public detail::HelperFunctions {
  private:
  public:
    static void format(const T &V, std::ostream &Stream, StringRef Style) {
        HexPrintStyle HS = HexPrintStyle::PrefixUpper;
        consume_hex_style(Style, HS);
        size_t Digits = consume_num_hex_digits(Style, HS, sizeof(void *) * 2);
        write_hex(Stream, reinterpret_cast<std::uintptr_t>(V), HS, Digits);
    }
};

/// Implementation of format_provider<T> for c-style strings and string
/// objects such as std::string and llvm::StringRef.
///
/// The options string of a string type has the grammar:
///
///   string_options :: [length]
///
/// where `length` is an optional integer specifying the maximum number of
/// characters in the string to print.  If `length` is omitted, the string is
/// printed up to the null terminator.

template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_string_formatter<T>::value>::type> {
    static void format(const T &V, std::ostream &Stream, StringRef Style) {
        size_t N = StringRef::npos;
        if (!Style.empty() && Style.getAsInteger(10, N)) {
            assert(false && "Style is not a valid integer");
        }
        StringRef S = V;
        Stream << S.substr(0, N);
    }
};

/// Implementation of format_provider<T> for characters.
///
/// The options string of a character type has the grammar:
///
///   char_options :: (empty) | [integer_options]
///
/// If `char_options` is empty, the character is displayed as an ASCII
/// character.  Otherwise, it is treated as an integer options string.
///
template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_char_formatter<T>::value>::type> {
    static void format(const char &V, std::ostream &Stream, StringRef Style) {
        if (Style.empty())
            Stream << V;
        else {
            int X = static_cast<int>(V);
            format_provider<int>::format(X, Stream, Style);
        }
    }
};

/// Implementation of format_provider<T> for type `bool`
///
/// The options string of a boolean type has the grammar:
///
///   bool_options :: "" | "Y" | "y" | "D" | "d" | "T" | "t"
///
///   ==================================
///   |    C    |     Meaning          |
///   ==================================
///   |    Y    |       YES / NO       |
///   |    y    |       yes / no       |
///   |  D / d  |    Integer 0 or 1    |
///   |    T    |     TRUE / FALSE     |
///   |    t    |     true / false     |
///   | (empty) |   Equivalent to 't'  |
///   ==================================
template <> struct format_provider<bool> {
    static void format(const bool &B, std::ostream &Stream, StringRef Style) {
        if (Style.starts_with("Y")) {
            Stream << (B ? "YES" : "NO");
        } else if (Style.starts_with("y")) {
            Stream << (B ? "yes" : "no");
        } else if (Style.starts_with("D")) {
            Stream << (B ? "1" : "0");
        } else if (Style.starts_with("T")) {
            Stream << (B ? "TRUE" : "FALSE");
        } else if (Style.starts_with("t")) {
            Stream << (B ? "true" : "false");
        } else {
            Stream << (B ? "1" : "0");
        }
    }
};

/// Implementation of format_provider<T> for floating point types.
///
/// The options string of a floating point type has the format:
///
///   float_options   :: [style][precision]
///   style           :: <see table below>
///   precision       :: <non-negative integer> 0-99
///
///   =====================================================
///   |  style  |     Meaning          |      Example     |
///   -----------------------------------------------------
///   |         |                      |  Input |  Output |
///   =====================================================
///   | P / p   | Percentage           |  0.05  |  5.00%  |
///   | F / f   | Fixed point          |   1.0  |  1.00   |
///   |   E     | Exponential with E   | 100000 | 1.0E+05 |
///   |   e     | Exponential with e   | 100000 | 1.0e+05 |
///   | (empty) | Same as F / f        |        |         |
///   =====================================================
///
/// The default precision is 6 for exponential (E / e) and 2 for everything
/// else.

template <typename T>
struct format_provider<
    T, typename std::enable_if<detail::use_double_formatter<T>::value>::type>
    : public detail::HelperFunctions {
    static void format(const T &V, std::ostream &Stream, StringRef Style) {
        FloatStyle S;
        if (Style.consume_front("P") || Style.consume_front("p"))
            S = FloatStyle::Percent;
        else if (Style.consume_front("F") || Style.consume_front("f"))
            S = FloatStyle::Fixed;
        else if (Style.consume_front("E"))
            S = FloatStyle::ExponentUpper;
        else if (Style.consume_front("e"))
            S = FloatStyle::Exponent;
        else
            S = FloatStyle::Fixed;

        std::optional<size_t> Precision = parse_numeric_precision(Style);
        if (!Precision.has_value())
            Precision = get_default_precision(S);

        write_double(Stream, static_cast<double>(V), S, Precision);
    }
};

enum class ReplacementType { Empty, Format, Literal };

struct ReplacementItem {
    ReplacementItem() = default;
    explicit ReplacementItem(StringRef Literal)
        : Type(ReplacementType::Literal), Spec(Literal) {}
    ReplacementItem(StringRef Spec, size_t Index, size_t Align,
                    AlignStyle Where, char Pad, StringRef Options)
        : Type(ReplacementType::Format), Spec(Spec), Index(Index), Align(Align),
          Where(Where), Pad(Pad), Options(Options) {}

    ReplacementType Type = ReplacementType::Empty;
    StringRef Spec;
    size_t Index = 0;
    size_t Align = 0;
    AlignStyle Where = AlignStyle::Right;
    char Pad;
    StringRef Options;
};

class formatv_object_base;
std::ostream &operator<<(std::ostream &, const formatv_object_base &);

class formatv_object_base {
  protected:
    // The parameters are stored in a std::tuple, which does not provide runtime
    // indexing capabilities.  In order to enable runtime indexing, we use this
    // structure to put the parameters into a std::vector.  Since the parameters
    // are not all the same type, we use some type-erasure by wrapping the
    // parameters in a template class that derives from a non-template
    // superclass. Essentially, we are converting a std::tuple<Derived<Ts...>>
    // to a std::vector<Base*>.
    struct create_adapters {
        template <typename... Ts>
        std::vector<detail::format_adapter *> operator()(Ts &...Items) {
            return std::vector<detail::format_adapter *>{&Items...};
        }
    };

    StringRef Fmt;
    std::vector<detail::format_adapter *> Adapters;
    std::vector<ReplacementItem> Replacements;

    static bool consumeFieldLayout(StringRef &Spec, AlignStyle &Where,
                                   size_t &Align, char &Pad) {
        Where = AlignStyle::Right;
        Align = 0;
        Pad = ' ';
        if (Spec.empty())
            return true;

        if (Spec.size() > 1) {
            // A maximum of 2 characters at the beginning can be used for
            // something other than the width. If Spec[1] is a loc char, then
            // Spec[0] is a pad char and Spec[2:...] contains the width.
            // Otherwise, if Spec[0] is a loc char, then Spec[1:...] contains
            // the width. Otherwise, Spec[0:...] contains the width.
            if (auto Loc = translateLocChar(Spec[1])) {
                Pad = Spec[0];
                Where = *Loc;
                Spec = Spec.drop_front(2);
            } else if (auto Loc = translateLocChar(Spec[0])) {
                Where = *Loc;
                Spec = Spec.drop_front(1);
            }
        }

        bool Failed = Spec.consumeInteger(0, Align);
        return !Failed;
    }

    static std::pair<ReplacementItem, StringRef>
    splitLiteralAndReplacement(StringRef Fmt) {
        std::size_t From = 0;
        while (From < Fmt.size() && From != StringRef::npos) {
            std::size_t BO = Fmt.find_first_of('{', From);
            // Everything up until the first brace is a literal.
            if (BO != 0)
                return std::make_pair(ReplacementItem{Fmt.substr(0, BO)},
                                      Fmt.substr(BO));

            StringRef Braces =
                Fmt.drop_front(BO).take_while([](char C) { return C == '{'; });
            // If there is more than one brace, then some of them are escaped.
            // Treat these as replacements.
            if (Braces.size() > 1) {
                size_t NumEscapedBraces = Braces.size() / 2;
                StringRef Middle = Fmt.substr(BO, NumEscapedBraces);
                StringRef Right = Fmt.drop_front(BO + NumEscapedBraces * 2);
                return std::make_pair(ReplacementItem{Middle}, Right);
            }
            // An unterminated open brace is undefined.  We treat the rest of
            // the string as a literal replacement, but we assert to indicate
            // that this is undefined and that we consider it an error.
            std::size_t BC = Fmt.find_first_of('}', BO);
            if (BC == StringRef::npos) {
                assert(false && "Unterminated brace sequence.  Escape with {{ "
                                "for a literal brace.");
                return std::make_pair(ReplacementItem{Fmt}, StringRef());
            }

            // Even if there is a closing brace, if there is another open brace
            // before this closing brace, treat this portion as literal, and try
            // again with the next one.
            std::size_t BO2 = Fmt.find_first_of('{', BO + 1);
            if (BO2 < BC)
                return std::make_pair(ReplacementItem{Fmt.substr(0, BO2)},
                                      Fmt.substr(BO2));

            StringRef Spec = Fmt.slice(BO + 1, BC);
            StringRef Right = Fmt.substr(BC + 1);

            auto RI = parseReplacementItem(Spec);
            if (RI.has_value())
                return std::make_pair(*RI, Right);

            // If there was an error parsing the replacement item, treat it as
            // an invalid replacement spec, and just continue.
            From = BC + 1;
        }
        return std::make_pair(ReplacementItem{Fmt}, StringRef());
    }

  public:
    formatv_object_base(StringRef Fmt, std::size_t ParamCount)
        : Fmt(Fmt), Replacements(parseFormatString(Fmt)) {
        Adapters.reserve(ParamCount);
    }

    formatv_object_base(formatv_object_base const &rhs) = delete;

    formatv_object_base(formatv_object_base &&rhs)
        : Fmt(std::move(rhs.Fmt)),
          Adapters(), // Adapters are initialized by formatv_object
          Replacements(std::move(rhs.Replacements)) {
        Adapters.reserve(rhs.Adapters.size());
    };

    void format(std::ostream &S) const {
        for (auto &R : Replacements) {
            if (R.Type == ReplacementType::Empty)
                continue;
            if (R.Type == ReplacementType::Literal) {
                S << R.Spec;
                continue;
            }
            if (R.Index >= Adapters.size()) {
                S << R.Spec;
                continue;
            }

            auto W = Adapters[R.Index];

            FmtAlign Align(*W, R.Where, R.Align, R.Pad);
            Align.format(S, R.Options);
        }
    }

    static std::optional<AlignStyle> translateLocChar(char C) {
        switch (C) {
        case '-':
            return AlignStyle::Left;
        case '=':
            return AlignStyle::Center;
        case '+':
            return AlignStyle::Right;
        default:
            return std::nullopt;
        }
    }

    static std::vector<ReplacementItem> parseFormatString(StringRef Fmt) {
        std::vector<ReplacementItem> Replacements;
        ReplacementItem I;
        while (!Fmt.empty()) {
            std::tie(I, Fmt) = splitLiteralAndReplacement(Fmt);
            if (I.Type != ReplacementType::Empty)
                Replacements.push_back(I);
        }
        return Replacements;
    }

    static std::optional<ReplacementItem> parseReplacementItem(StringRef Spec) {
        StringRef RepString = Spec.trim("{}");

        // If the replacement sequence does not start with a non-negative
        // integer, this is an error.
        char Pad = ' ';
        std::size_t Align = 0;
        AlignStyle Where = AlignStyle::Right;
        StringRef Options;
        size_t Index = 0;
        RepString = RepString.trim();
        if (RepString.consumeInteger(0, Index)) {
            assert(false && "Invalid replacement sequence index!");
            return ReplacementItem{};
        }
        RepString = RepString.trim();
        if (!RepString.empty() && RepString.front() == ',') {
            RepString = RepString.drop_front();
            if (!consumeFieldLayout(RepString, Where, Align, Pad))
                assert(false &&
                       "Invalid replacement field layout specification!");
        }
        RepString = RepString.trim();
        if (!RepString.empty() && RepString.front() == ':') {
            Options = RepString.drop_front().trim();
            RepString = StringRef();
        }
        RepString = RepString.trim();
        if (!RepString.empty()) {
            assert(false &&
                   "Unexpected characters found in replacement string!");
        }

        return ReplacementItem{Spec, Index, Align, Where, Pad, Options};
    }

    std::string str() const {
        std::string Result;
        std::stringstream Stream(Result);
        Stream << *this;
        Stream.flush();
        return Result;
    }

    operator std::string() const {
        return str();
    }
};

std::ostream &operator<<(std::ostream &os, const formatv_object_base &base) {
    base.format(os);
    return os;
}

template <typename Tuple> class formatv_object : public formatv_object_base {
    // Storage for the parameter adapters.  Since the base class erases the
    // type of the parameters, we have to own the storage for the parameters
    // here, and have the base class store type-erased pointers into this
    // tuple.
    Tuple Parameters;

  public:
    formatv_object(StringRef Fmt, Tuple &&Params)
        : formatv_object_base(Fmt, std::tuple_size<Tuple>::value),
          Parameters(std::move(Params)) {
        Adapters = apply_tuple(create_adapters(), Parameters);
    }

    formatv_object(formatv_object const &rhs) = delete;

    formatv_object(formatv_object &&rhs)
        : formatv_object_base(std::move(rhs)),
          Parameters(std::move(rhs.Parameters)) {
        Adapters = apply_tuple(create_adapters(), Parameters);
    }
};

// Format text given a format string and replacement parameters.
//
// ===General Description===
//
// Formats textual output.  `Fmt` is a string consisting of one or more
// replacement sequences with the following grammar:
//
// rep_field ::= "{" [index] ["," layout] [":" format] "}"
// index     ::= <non-negative integer>
// layout    ::= [[[char]loc]width]
// format    ::= <any string not containing "{" or "}">
// char      ::= <any character except "{" or "}">
// loc       ::= "-" | "=" | "+"
// width     ::= <positive integer>
//
// index   - A non-negative integer specifying the index of the item in the
//           parameter pack to print.  Any other value is invalid.
// layout  - A string controlling how the field is laid out within the available
//           space.
// format  - A type-dependent string used to provide additional options to
//           the formatting operation.  Refer to the documentation of the
//           various individual format providers for per-type options.
// char    - The padding character.  Defaults to ' ' (space).  Only valid if
//           `loc` is also specified.
// loc     - Where to print the formatted text within the field.  Only valid if
//           `width` is also specified.
//           '-' : The field is left aligned within the available space.
//           '=' : The field is centered within the available space.
//           '+' : The field is right aligned within the available space (this
//                 is the default).
// width   - The width of the field within which to print the formatted text.
//           If this is less than the required length then the `char` and `loc`
//           fields are ignored, and the field is printed with no leading or
//           trailing padding.  If this is greater than the required length,
//           then the text is output according to the value of `loc`, and padded
//           as appropriate on the left and/or right by `char`.
//
// ===Special Characters===
//
// The characters '{' and '}' are reserved and cannot appear anywhere within a
// replacement sequence.  Outside of a replacement sequence, in order to print
// a literal '{' or '}' it must be doubled -- "{{" to print a literal '{' and
// "}}" to print a literal '}'.
//
// ===Parameter Indexing===
// `index` specifies the index of the parameter in the parameter pack to format
// into the output.  Note that it is possible to refer to the same parameter
// index multiple times in a given format string.  This makes it possible to
// output the same value multiple times without passing it multiple times to the
// function. For example:
//
//   formatv("{0} {1} {0}", "a", "bb")
//
// would yield the string "abba".  This can be convenient when it is expensive
// to compute the value of the parameter, and you would otherwise have had to
// save it to a temporary.
//
// ===Formatter Search===
//
// For a given parameter of type T, the following steps are executed in order
// until a match is found:
//
//   1. If the parameter is of class type, and inherits from format_adapter,
//      Then format() is invoked on it to produce the formatted output.  The
//      implementation should write the formatted text into `Stream`.
//   2. If there is a suitable template specialization of format_provider<>
//      for type T containing a method whose signature is:
//      void format(const T &Obj, std::ostream &Stream, StringRef Options)
//      Then this method is invoked as described in Step 1.
//   3. If an appropriate operator<< for std::ostream exists, it will be used.
//      For this to work, (std::ostream& << const T&) must return std::ostream&.
//
// If a match cannot be found through either of the above methods, a compiler
// error is generated.
//
// ===Invalid Format String Handling===
//
// In the case of a format string which does not match the grammar described
// above, the output is undefined.  With asserts enabled, LLVM will trigger an
// assertion.  Otherwise, it will try to do something reasonable, but in general
// the details of what that is are undefined.
//
template <typename... Ts>
inline auto formatv(const char *Fmt, Ts &&...Vals)
    -> formatv_object<decltype(std::make_tuple(
        detail::build_format_adapter(std::forward<Ts>(Vals))...))> {
    using ParamTuple = decltype(std::make_tuple(
        detail::build_format_adapter(std::forward<Ts>(Vals))...));
    return formatv_object<ParamTuple>(
        Fmt, std::make_tuple(
                 detail::build_format_adapter(std::forward<Ts>(Vals))...));
}

int main(int argc, char *argv[]) {
    std::cout << formatv("Hello {1}, Pi = {0}!", 3.14, "Word") << std::endl;
    return 0;
}