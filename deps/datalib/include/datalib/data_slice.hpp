/*
 *  Copyright (C) 2014 Denilson das Merc�s Amorim (aka LINK/2012)
 *  Licensed under the Boost Software License v1.0 (http://opensource.org/licenses/BSL-1.0)
 *
 */
#pragma once
#include <functional>
#include <bitset>
#include <sstream>
#include <type_traits>
#include <datalib/detail/stream/memstream.hpp>
#include <datalib/detail/stream/kstream.hpp>
#include <datalib/detail/mpl/seqeach.hpp>
#include <datalib/detail/mpl/type_complexity_sort.hpp>
#include <datalib/data_info.hpp>
#include <datalib/data_info/string.hpp>

namespace datalib {

/*
 *  data_slice
 *      Stores a single piece of data (i.e. a single line).
 *      The piece of data stored is specified by the 'Types' variadic template argument.
 *
 *      [*] Each type needs a data_info<> specialization (see data_info.hpp) and should be able to I/O in icheckstream/imemstream/ostream
 *          using the overloaded shifting operators.
 *      [*] The type delimopt determines that the types following it are optional (i.e. the line might or might not contain them)
 */
template<typename ...Types>
class data_slice
{
    public:
        // Tuple used to store the types
        using tuple_type                = std::tuple<Types...>;

        // Number of types in 'Types'
        static const size_t tuple_size  = std::tuple_size<tuple_type>::value;

        // The bitset type used to determine whether a certain type has been fetched
        using bitset_type               = std::bitset<tuple_size>;              

        // 'sorted_type_indices' is a integer_sequence with the tuple indices sorted by complexity (less complexies first)
        using sorted_type_indices       = typename type_complexity_sort<tuple_type>::type;

    protected:
        tuple_type  tuple;              // Tuple to store the types
        bitset_type used;               // Bitset determining whether a certain tuple indice is in use
        size_t      used_count = 0;     // Number of bits set in the bitset

    public:

        //
        //  Constructors
        //

        data_slice() = default;

        data_slice(const data_slice& rhs)
            : tuple(rhs.tuple), used(rhs.used), used_count(rhs.used_count)
        {}

        data_slice(data_slice&& rhs)
            : tuple(std::move(rhs.tuple)), used(std::move(rhs.used)), used_count(rhs.used_count)
        {
            rhs.used_count = 0;
        }

        //
        //  Assignment Operators
        //

        data_slice& operator=(const data_slice& rhs)
        {
            this->tuple = rhs.tuple;
            this->used  = rhs.used;
            this->used_count = rhs.used_count;
            return *this;
        }

        data_slice& operator=(data_slice&& rhs)
        {
            this->tuple = std::move(rhs.tuple);
            this->used  = std::move(rhs.used);
            this->used_count = rhs.used_count;
            rhs.used_count = 0;
            return *this;
        }


        //
        // The following public members are the only accessible stuff in the class and the only stuff that can be overriden 
        // When I say overriden it's not in the polymorphism sense, but hide the base method because the derived implements the same method
        //

        // Determines whether 'this' contains the same data as 'rhs'
        // This is just a forwarder to 'this->equal_to(rhs)'
        // Derived classes MUST override and (probably) do the same
        bool operator==(const data_slice& rhs) const
        {
            return this->equal_to(rhs);
        }

        // Sets the content of this data storer to be what's on the line (by interpreting it)
        // Returns false on failure
        bool set(const std::string& line)
        {
            return scan_to_tuple(line) >= min_count();
        }

        // Gets the content of this data storer into the line (by disassembling it)
        // Returns false on failure
        bool get(std::string& line) const
        {
            return print_from_tuple(line) == this->count();
        }

        // Cheaply checks whether the content of 'line' can be stored in this data storer
        // If this return true, mostly like 'this->set(line)' will also return true
        bool check(const std::string& line) const
        {
            auto this_ = const_cast<data_slice*>(this);              // and here we go breaking constness
            return this_->check_on_tuple(line) >= min_count();
        }

        // Gets an element from the data tuple
        template<int I>
        auto get() -> decltype(std::get<I>(std::declval<tuple_type>()))
        {
            using return_type = decltype(std::get<I>(std::declval<tuple_type>()));
            return std::forward<return_type>(std::get<I>(this->tuple));
        }

        // Checks whether this object contains data stored in it
        bool good() const
        {
            return this->count() >= this->min_count();
        }

        // Determines the number of types being stored in this data storer
        int count() const
        {
            return this->used_count;
        }

        // Determines the number of optional types being stored in this data storer
        int optcount() const
        {
            if(this->used_count > min_count())      // if greater than min_count we have optional items, otherwise nope
                return used_count - min_count();
            return 0;
        }

        // Determiens the minimum number of types that should be stored in this data storer
        static int min_count()
        {
            static const auto count = find_min_count();
            return count;
        }

        // Determines the maximum number of types that can be stored in this data storer
        static int max_count()
        {
            static const auto count = find_max_count();
            return count;
        }

    protected:

        // Determines whether 'this' contains the same data as 'rhs'
        bool equal_to(const data_slice& rhs) const
        {
            if(this->count() == rhs.count())
            {
                if(precompare_data(*this, rhs))
                    return compare_data/*<std::equal_to>*/(*this, rhs);
            }
            return false;
        }


    private:

        // Scans the content of 'line' to 'this->tuple' and returns the amount of type successfully scanned
        int scan_to_tuple(const std::string& line)
        {
            imemstream stream(line, std::ios::in);
            scany_to_tuple<false> scanner(*this, stream);   // Scanner functor
            used.reset(); this->used_count = 0;             // Reset used bitset and counter
            foreach_in_tuple(tuple, scanner);               // Perform the scanning (this rearranges the bitset)
            return (this->used_count = scanner.counter);    // Re-set the counter
        }

        // Prints the content of 'this->tuple' to the 'line' and returns the amount of types successfully printed
        int print_from_tuple(std::string& line) const
        {
            // TODO make a output stream which returns the output string as reference (will we?)
            std::stringstream stream(std::ios::out);
            printy_from_tuple printer(*this, stream);       // Priter functor
            foreach_in_tuple(const_cast<tuple_type&>(tuple), printer);
            line = stream.str();
            return printer.counter;
        }

        // Checks how many types are possible to scan from 'line' into 'this->tuple'
        int check_on_tuple(const std::string& line)
        {
            icheckstream stream(line);
            scany_to_tuple<true> checker(*this, stream);
            foreach_in_tuple(tuple, checker);
            return checker.counter;
        }

        // Compares the data between the data storer 'lhs' and 'rhs' using the specified 'Pred'icate
        /*template<template<class> class Pred>*/
        static bool compare_data(const data_slice& lhs, const data_slice& rhs)
        {
            data_comparer</*Pred*/> comp(lhs, rhs);
            foreach_type<sorted_type_indices>()(comp);
            return comp.result;
        }

        // Precompares the data between the data storer 'lhs' and 'rhs'
        // Precomparing is cheap and should come BEFORE an compare_data<std::equal_to>(lhs, rhs)
        static bool precompare_data(const data_slice& lhs, const data_slice& rhs)
        {
            data_comparer</*data_info_precomparer, */true> comp(lhs, rhs);
            foreach_type<sorted_type_indices>()(comp);
            return comp.result;
        }

        // Counts the number of types present until the type 'Until' is found
        template<typename Until>
        static int do_count_until()
        {
            count_until_type<Until> f;
            foreach_type_variadic<Types...>()(f);
            return f.counter;
        }

        // Finds the minimum number of types that should be stored in this data storer
        static int find_min_count()
        {
            return do_count_until<delimopt>();
        }

        // Finds the maximum number of types that can be stored in this data storer
        static int find_max_count()
        {
            struct dummy_type { };   // will never find this type, so it finds all args
            return do_count_until<dummy_type>();
        }


    private:

        // CXX14 HELP-ME

        // Scans to tuple
        template<bool checker>  // checker is whether it's actual scanning or just checking
        struct scany_to_tuple
        {
            using stream_type = typename std::conditional<checker, icheckstream, imemstream>::type;

            data_slice&   self;
            stream_type& stream;
            int counter;

            scany_to_tuple(data_slice& self, stream_type& stream)
                : self(self), stream(stream), counter(0)
            {}

            // Scans tuple index
            template<class Integral, class TypeWr>
            bool operator()(Integral, TypeWr, typename TypeWr::type& value)
            {
                perform_scan<Integral::value, typename TypeWr::type>(value);
                return !!stream;
            }

            // This specialization runs if the type should be ignored
            template<int N, class Type>
            void perform_scan(typename std::enable_if<data_info<Type>::ignore, Type>::type& value)
            {
            }

            // This specialization runs if the type shouldn't be ignored
            template<int N, class Type>
            void perform_scan(typename std::enable_if<!data_info<Type>::ignore, Type>::type& value)
            {
                stream >> value;
                if(stream)
                {
                    ++counter;
                    if(!checker) self.used.set(N);  // checker shouldn't set any state at 'self'
                }
            }
        };

        // Prints from tuple
        struct printy_from_tuple
        {
            const data_slice&    self;
            std::stringstream&  stream;
            int                 counter;
            
            printy_from_tuple(const data_slice& self, std::stringstream& stream)
                : self(self), stream(stream), counter(0)
            {}

            // Prints tuple index
            template<class Integral, class TypeWr>
            bool operator()(Integral, TypeWr, typename TypeWr::type& value)
            {
                if(self.used.at(Integral::value))
                {
                    perform_print<Integral::value, TypeWr::type>(value);
                }
                return !!stream;
            }

            // This specialization runs if the type should be ignored
            template<int N, class Type>
            void perform_print(const typename std::enable_if<data_info<Type>::ignore, Type>::type& value)
            {
            }

            // This specialization runs if the type shouldn't be ignored
            template<int N, class Type>
            void perform_print(const typename std::enable_if<!data_info<Type>::ignore, Type>::type& value)
            {
                auto separator = data_info<Type>::separator;
                stream << value;
                if(stream)    ++counter;
                if(separator) stream << separator;
            }
        };

        

        // Compares the data from two data storers
        template<bool IsPreComparer = false>    // CANNOT USE PREDICATE, NEEDS REWRITE WITH THAT IN MIND
        struct data_comparer
        {
            bool result;
            int counter, max_counter;

            const data_slice& lhs;
            const data_slice& rhs;
            data_comparer(const data_slice& self, const data_slice& rhs)
                : lhs(self), rhs(rhs), result(IsPreComparer? true : false), counter(0)
            {
                this->max_counter = (lhs.count() == rhs.count())? lhs.count() : (std::numeric_limits<int>::max)();
            }


            template<int N> // compares element N of the tuple
            bool compare()
            {
                static const bool has_precompare =
                    detail::data_info_precomparer<>::precompare<std::decay<decltype(std::get<N>(lhs.tuple))>::type>::has_precompare;

                // If is pre comparing, check if this type can perform precomparision
                // If not pre comparing, just ignore this check
                if(!IsPreComparer || has_precompare)
                {
                    bool lhs_used = lhs.used.at(N);
                    bool rhs_used = rhs.used.at(N);

                    if(lhs_used == rhs_used)    // it should have the same state at both sides for it to be equal
                    {
                        if(lhs_used)    // being actually used?
                        {
                            if(std::equal_to<void>()(std::get<N>(lhs.tuple), std::get<N>(rhs.tuple)))
                            {
                                if(++counter >= max_counter)
                                {
                                    this->result = true;
                                    return false;   // stop iteration
                                }
                            }
                            else
                            {
                                this->result = false;
                                return false; // stop iteration
                            }
                        }
                    }
                    else
                    {
                        this->result = false;
                        return false; // stop iteration
                    }
                }
                return true;
            }

            template<class Index, class Integral>
            bool operator()(Index, Integral)
            {
                return compare<Integral::type::value>();
            }
        };

        // Functor used to count the number of types received until the type 'Until' is received
        template<class Until>
        struct count_until_type
        {
            int counter = 0;

            template<class Index, class TypeW>
            bool operator()(Index, TypeW)
            {
                using Type = TypeW::type;
                if(!data_info<Type>::ignore)
                    ++counter;
                else if(std::is_same<Type, Until>::value)
                    return false;
                return true;
            }
        };

};


// CXX14 HELP-ME

template<int I, class ...Types> inline
auto get(data_slice<Types...>& data) -> decltype(std::declval<data_slice<Types...>>().get<I>())
{
    using return_type = decltype(std::declval<data_slice<Types...>>().get<I>());
    return std::forward<return_type>(data.get<I>());
}

template<int I, class ...Types> inline
auto get(const data_slice<Types...>& data) -> std::add_const_t<decltype(get<I>(std::declval<data_slice<Types...>>()))>
{
    using return_type = decltype(get<I>(std::declval<data_slice<Types...>>()));
    return std::forward<return_type>(get<I>(const_cast<data_slice<Types...>&>(data)));
}





} // namespace datalib
