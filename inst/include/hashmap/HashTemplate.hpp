// vim: set softtabstop=4:expandtab:number:syntax on:wildmenu:showmatch
//
// HashTemplate.hpp
//
// Copyright (C) 2016 Nathan Russell
//
// This file is part of hashmap.
//
// hashmap is free software: you can redistribute it and/or
// modify it under the terms of the MIT License.
//
// hashmap is provided "as is", without warranty of any kind,
// express or implied, including but not limited to the
// warranties of merchantability, fitness for a particular
// purpose and noninfringement.
//
// You should have received a copy of the MIT License
// along with hashmap. If not, see
// <https://opensource.org/licenses/MIT>.

#ifndef hashmap__HashTemplate__hpp
#define hashmap__HashTemplate__hpp

#include "traits.hpp"
#include "as_character.hpp"
#include <boost/unordered_map.hpp>
#include <boost/functional/hash.hpp>

namespace hashmap {

template <int RTYPE>
inline typename traits::cpp_traits<RTYPE>::type
extractor(const Rcpp::Vector<RTYPE>& vec, R_xlen_t i) {
    return vec[i];
}

template <>
inline std::string
extractor<STRSXP>(const Rcpp::Vector<STRSXP>& vec, R_xlen_t i) {
    return Rcpp::as<std::string>(vec[i]);
}

template <typename KeyType, typename ValueType>
class HashTemplate {
public:
    typedef KeyType key_t;
    typedef ValueType value_t;
    typedef boost::unordered_map<key_t, value_t> map_t;

    enum { key_rtype = traits::sexp_traits<key_t>::rtype };
    enum { value_rtype = traits::sexp_traits<value_t>::rtype };

    typedef Rcpp::Vector<key_rtype> key_vec;
    typedef Rcpp::Vector<value_rtype> value_vec;

    typedef typename map_t::size_type size_type;
    typedef typename map_t::const_iterator const_iterator;
    typedef typename map_t::iterator iterator;
    typedef typename map_t::hasher hasher;

private:
    map_t map;

    key_t key_na() const { return traits::get_na<key_t>(); }
    value_t value_na() const { return traits::get_na<value_t>(); }

    mutable bool keys_cached_;
    mutable bool values_cached_;

    mutable key_vec kvec;
    mutable value_vec vvec;

    mutable bool date_keys;
    mutable bool date_values;

    struct posix_t {
        bool is;
        Rcpp::RObject tz;

        template <int RTYPE>
        posix_t(const Rcpp::Vector<RTYPE>& x)
            : is(Rf_inherits(x, "POSIXt")),
              tz(R_NilValue)
        {
            if (!is) return;
            tz = Rf_isNull(x.attr("tzone")) ?
                Rcpp::wrap("UTC") : x.attr("tzone");
        }

        posix_t()
            : is(false), tz(R_NilValue)
        {}

        posix_t(const posix_t& other)
            : is(other.is),
              tz(Rcpp::clone(other.tz))
        {}
    };

    posix_t posix_keys;
    posix_t posix_values;

    HashTemplate(const map_t& xmap, bool xkeys_cached_, bool xvalues_cached_,
                 const key_vec& xkvec, const value_vec& xvvec,
                 bool xdate_keys, bool xdate_values,
                 const posix_t& xposix_keys, const posix_t& xposix_values)
        : map(xmap),
          keys_cached_(xkeys_cached_),
          values_cached_(xvalues_cached_),
          kvec(Rcpp::clone(xkvec)),
          vvec(Rcpp::clone(xvvec)),
          date_keys(xdate_keys),
          date_values(xdate_values),
          posix_keys(xposix_keys),
          posix_values(xposix_values)
    {}

    void set_key_attr(key_vec& x) const {
        if (date_keys) {
            x.attr("class") = "Date";
        } else if (posix_keys.is) {
            x.attr("class") =
                Rcpp::CharacterVector::create("POSIXct", "POSIXt");
            x.attr("tzone") = posix_keys.tz;
        }
    }

    void set_value_attr(value_vec& x) const {
        if (date_values) {
            x.attr("class") = "Date";
        } else if (posix_values.is) {
            x.attr("class") =
                Rcpp::CharacterVector::create("POSIXct", "POSIXt");
            x.attr("tzone") = posix_keys.tz;
        }
    }

public:
    HashTemplate()
        : keys_cached_(false), values_cached_(false),
          date_keys(false), date_values(false),
          posix_keys(posix_t()), posix_values(posix_t())
    {
        kvec = key_vec(0);
        vvec = value_vec(0);
    }

    HashTemplate(const key_vec& keys_, const value_vec& values_)
        : keys_cached_(false), values_cached_(false),
          posix_keys(keys_), posix_values(values_)
    {
        R_xlen_t nk = keys_.size(), nv = values_.size(), i = 0, n;
        if (nk != nv) {
            Rcpp::warning("length(keys) != length(values)!");
        }
        n = nk < nv ? nk : nv;

        map.reserve((size_type)(n * 1.05));
        kvec = key_vec(n);
        vvec = value_vec(n);

        for ( ; i < n; i++) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            map[extractor(keys_, i)] = extractor(values_, i);
        }

        date_keys = Rf_inherits(keys_, "Date");
        date_values = Rf_inherits(values_, "Date");
    }

    HashTemplate clone() const {
        return HashTemplate(
            map, keys_cached_, values_cached_,
            kvec, vvec, date_keys, date_values,
            posix_keys, posix_values
        );
    }

    size_type size() const { return map.size(); }
    bool empty() const { return map.empty(); }
    bool keys_cached() const { return keys_cached_; }
    bool values_cached() const { return values_cached_; }
    int key_sexptype() const { return key_rtype; }
    int value_sexptype() const { return value_rtype; }

    void clear() {
        map.clear();
        keys_cached_ = false;
        values_cached_ = false;
    }

    size_type bucket_count() const { return map.bucket_count(); }
    void rehash(size_type n) { map.rehash(n); }
    void reserve(size_type n) { map.reserve(n); }

    Rcpp::Vector<INTSXP> hash_value(const key_vec& keys_) const {
        R_xlen_t i = 0, nk = keys_.size();
        Rcpp::Vector<INTSXP> res = Rcpp::no_init_vector(nk);
        hasher h;

        for ( ; i < nk; i++) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            res[i] = h(extractor(keys_, i));
        }

        return res;
    }

    void insert(const key_vec& keys_, const value_vec& values_) {
        R_xlen_t nk = keys_.size(), nv = values_.size(), i = 0, n;
        if (nk != nv) {
            Rcpp::warning("length(keys) != length(values)!");
        }
        n = nk < nv ? nk : nv;
        keys_cached_ = false;
        values_cached_ = false;

        for ( ; i < n; i++) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            map[extractor(keys_, i)] = extractor(values_, i);
        }
    }

    void insert(SEXP keys_, SEXP values_) {
        insert(Rcpp::as<key_vec>(keys_), Rcpp::as<value_vec>(values_));
    }

    key_vec keys() const {
        if (keys_cached_) {
            return kvec;
        }

        const_iterator first = map.begin(), last = map.end();
        key_vec res(map.size());

        for (R_xlen_t i = 0; first != last; ++first) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            res[i++] = first->first;
        }

        set_key_attr(res);

        kvec = res;
        keys_cached_ = true;

        return res;
    }

    key_vec keys_n(int nx) const {
        if (nx < 0) nx = 0;
        if ((size_type)nx > map.size()) nx = map.size();

        if (keys_cached_) {
            key_vec res = kvec[Rcpp::seq(0, nx - 1)];
            set_key_attr(res);
            return res;
        }

        const_iterator first = map.begin(), last = map.end();
        key_vec res(nx);

        for (R_xlen_t i = 0; first != last && i < nx; ++first) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            res[i++] = first->first;
        }

        set_key_attr(res);

        return res;
    }

    value_vec values() const {
        if (values_cached_) {
            return vvec;
        }

        const_iterator first = map.begin(), last = map.end();
        value_vec res(map.size());

        for (R_xlen_t i = 0; first != last; ++first) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            res[i++] = first->second;
        }

        set_value_attr(res);

        vvec = res;
        values_cached_ = true;

        return res;
    }

    value_vec values_n(int nx) const {
        if (nx < 0) nx = 0;
        if ((size_type)nx > map.size()) nx = map.size();

        if (values_cached_) {
            value_vec res =  vvec[Rcpp::seq(0, nx - 1)];
            set_value_attr(res);
            return res;
        }

        const_iterator first = map.begin(), last = map.end();
        value_vec res(nx);

        for (R_xlen_t i = 0; first != last && i < nx; ++first) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            res[i++] = first->second;
        }

        set_value_attr(res);

        return res;
    }

    void cache_keys() {
        if (keys_cached_) return;

        R_xlen_t i = 0, n = map.size();
        if (kvec.size() != n) {
            kvec = key_vec(n);
        }

        const_iterator first = map.begin(), last = map.end();
        for ( ; first != last; ++first) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            kvec[i++] = first->first;
        }

        set_key_attr(kvec);
        keys_cached_ = true;
    }

    void cache_values() {
        if (values_cached_) return;

        R_xlen_t i = 0, n = map.size();
        if (vvec.size() != n) {
            vvec = value_vec(n);
        }

        const_iterator first = map.begin(), last = map.end();
        for ( ; first != last; ++first) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            vvec[i++] = first->second;
        }

        set_value_attr(vvec);
        values_cached_ = true;
    }

    void erase(const key_vec& keys_) {
        R_xlen_t i = 0, n = keys_.size();

        for ( ; i < n; i++) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            map.erase(extractor(keys_, i));
        }

        keys_cached_ = false;
        values_cached_ = false;
    }

    value_vec find(const key_vec& keys_) const {
        R_xlen_t i = 0, n = keys_.size();
        value_vec res(n);
        const_iterator last = map.end();

        for ( ; i < n; i++) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            const_iterator pos = map.find(extractor(keys_, i));
            if (pos != last) {
                res[i] = pos->second;
            } else {
                res[i] = Rcpp::traits::get_na<value_rtype>();
            }
        }

        set_value_attr(res);
        return res;
    }

    value_vec find(SEXP keys_) const {
        return find(Rcpp::as<key_vec>(keys_));
    }

    bool has_key(const key_vec& keys_) const {
        return map.find(extractor(keys_, 0)) != map.end();
    }

    bool has_key(SEXP keys_) const {
        return has_key(Rcpp::as<key_vec>(keys_));
    }

    Rcpp::Vector<LGLSXP> has_keys(const key_vec& keys_) const {
        R_xlen_t i = 0, n = keys_.size();
        Rcpp::Vector<LGLSXP> res = Rcpp::no_init_vector(n);
        const_iterator last = map.end();

        for ( ; i < n; i++) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            res[i] = (map.find(extractor(keys_, i)) != last) ?
                true : false;
        }

        return res;
    }

    Rcpp::Vector<LGLSXP> has_keys(SEXP keys_) const {
        return has_keys(Rcpp::as<key_vec>(keys_));
    }

    value_vec data() const {
        if (values_cached_ && keys_cached_) {
            value_vec res(vvec);
            res.names() = kvec;
            return res;
        }

        R_xlen_t i = 0, n = map.size();

        value_vec res(n);
        key_vec knames(n);

        const_iterator first = map.begin(), last = map.end();

        for ( ; first != last; ++first) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            knames[i] = first->first;
            res[i] = first->second;
            ++i;
        }

        set_value_attr(res);

        if (date_keys) {
            knames.attr("class") = "Date";
        } else if (posix_keys.is) {
            knames.attr("class") =
                Rcpp::CharacterVector::create("POSIXct", "POSIXt");
            knames.attr("tzone") = posix_keys.tz;

            res.names() = knames;
            return res;
        }

        SEXP snames;
        PROTECT(snames = Rf_coerceVector(knames, STRSXP));
        Rcpp::Vector<STRSXP> names(snames);
        UNPROTECT(1);

        res.names() = names;
        return res;
    }

    value_vec data_n(int nx) const {
        if (nx < 0) nx = 0;
        if ((size_type)nx > map.size()) nx = map.size();

        if (values_cached_ && keys_cached_) {
            Rcpp::Range vidx = Rcpp::seq(0, nx - 1);
            value_vec res = vvec[vidx];
            res.names() = kvec[vidx];
            return res;
        }

        R_xlen_t i = 0, n = 0;

        value_vec res(nx);
        key_vec knames(nx);

        const_iterator first = map.begin(), last = map.end();

        for ( ; first != last && n != nx; ++first, ++n) {
            HASHMAP_CHECK_INTERRUPT(i, 50000);
            knames[i] = first->first;
            res[i] = first->second;
            ++i;
        }

        set_value_attr(res);

        if (date_keys) {
            knames.attr("class") = "Date";
        } else if (posix_keys.is) {
            knames.attr("class") =
                Rcpp::CharacterVector::create("POSIXct", "POSIXt");
            knames.attr("tzone") = posix_keys.tz;

            res.names() = knames;
            return res;
        }

        SEXP snames;
        PROTECT(snames = Rf_coerceVector(knames, STRSXP));
        Rcpp::Vector<STRSXP> names(snames);
        UNPROTECT(1);

        res.names() = names;
        return res;
    }

    Rcpp::DataFrame data_frame() const {
        if (keys_cached_ && values_cached_) {
            return Rcpp::DataFrame::create(
                Rcpp::Named("Keys") = kvec,
                Rcpp::Named("Values") = vvec
            );
        }

        if (keys_cached_) {
            return Rcpp::DataFrame::create(
                Rcpp::Named("Keys") = kvec,
                Rcpp::Named("Values") = values()
            );
        }

        if (values_cached_) {
            return Rcpp::DataFrame::create(
                Rcpp::Named("Keys") = keys(),
                Rcpp::Named("Values") = vvec
            );
        }

        return Rcpp::DataFrame::create(
            Rcpp::Named("Keys") = keys(),
            Rcpp::Named("Values") = values()
        );
    }

    value_vec na_value_vector(R_xlen_t sz) const {
        return value_vec(sz, value_na());
    }

    std::string key_class_name() const {
        if (date_keys) return "Date";
        if (posix_keys.is) return "POSIXct";
        switch ((int)key_rtype) {
            case INTSXP: return "integer";
            case REALSXP: return "numeric";
            case STRSXP: return "character";
            case LGLSXP: return "logical";
            case CPLXSXP: return "complex";
            default: return "";
        }
        return "";
    }

    std::string value_class_name() const {
        if (date_values) return "Date";
        if (posix_values.is) return "POSIXct";
        switch ((int)value_rtype) {
            case INTSXP: return "integer";
            case REALSXP: return "numeric";
            case STRSXP: return "character";
            case LGLSXP: return "logical";
            case CPLXSXP: return "complex";
            default: return "";
        }
        return "";
    }

    template <typename KT, typename VT>
    Rcpp::DataFrame left_outer_join(const HashTemplate<KT, VT>& other) const {
        if (empty()) return Rcpp::DataFrame::create();

        Rcpp::DataFrame res = Rcpp::DataFrame::create(
            Rcpp::Named("Keys") = keys(),
            Rcpp::Named("Values.x") = values(),
            Rcpp::Named("Values.y") = other.na_value_vector(size())
        );

        std::string lhs_kcn = key_class_name(),
            rhs_kcn = other.key_class_name();

        if (lhs_kcn != rhs_kcn) {
            Rcpp::warning(
                "Attempt to join different key types: %s and %s\n",
                lhs_kcn.c_str(),
                rhs_kcn.c_str()
            );
            return res;
        }

        res[2] = other.find(kvec);
        return res;
    }

    // TODO: move to HashMap.hpp & use XPtr<HashMap>
    //  cannot use XPtr<HashTemplate> from R
    //
    // template <typename KT, typename VT> Rcpp::DataFrame
    // left_outer_join(const Rcpp::XPtr< HashTemplate<KT, VT> >& other) const {
    //     return left_outer_join(*other);
    // }
};

typedef HashTemplate<std::string, std::string> ss_hash;
typedef HashTemplate<std::string, double> sd_hash;
typedef HashTemplate<std::string, int> si_hash;
typedef HashTemplate<std::string, bool> sb_hash;
typedef HashTemplate<std::string, Rcomplex> sx_hash;

typedef HashTemplate<double, double> dd_hash;
typedef HashTemplate<double, std::string> ds_hash;
typedef HashTemplate<double, int> di_hash;
typedef HashTemplate<double, bool> db_hash;
typedef HashTemplate<double, Rcomplex> dx_hash;

typedef HashTemplate<int, int> ii_hash;
typedef HashTemplate<int, std::string> is_hash;
typedef HashTemplate<int, double> id_hash;
typedef HashTemplate<int, bool> ib_hash;
typedef HashTemplate<int, Rcomplex> ix_hash;

} // hashmap

#endif // hashmap__HashTemplate__hpp
