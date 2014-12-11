/*
 *  Copyright (C) 2014 Denilson das Merc�s Amorim (aka LINK/2012)
 *  Licensed under the Boost Software License v1.0 (http://opensource.org/licenses/BSL-1.0)
 *
 */
#pragma once
#include <datalib/data_store.hpp>
#include <datalib/gta3/data_section.hpp>


namespace datalib {
namespace gta3 {

/*
 *  gta3::data_store
 *      Special datalib::data_store which stores data sets depending on TraitsType specifications.
 *      That's whether the data set has sections, has key-value pair inversed, etc
 *
 *      This class is final, every possibly derived work should be perfomed by the TraitsType!
 *      The reason behind it being final is because this type is sent as template parameter to many places.
 *
 *      See datalib::data_store for more details on this class role.
 *
 *      TraitsType details:
 *      {
 *
 *          The traits may contain non-static data, it is instantiated and copied together this store.
 *          The following should be implemented in the traits to meet the gta3::data_store::traits_type concept
 *
 *          static const bool has_sections      -> Whether this store have sections (as in .ide files)
 *          static const bool per_line_section  -> Whether each line is a different section (as in gta.dat)
 *          static const bool is_reversed_kv    -> Whether the methods that would go in the value element is in the key element.
 *                                                 This basically means the key stores the data instead of the value.
 *
 *          Key key_from_value(Value) or Value value_from_key(Key)
 *                                              -> Return the key/value based on the value/key
 *                                                  Which one you should implement depends on 'is_reversed_kv'
 *                                                  Those methods may be non-static
 *
 *          static const section_info* sections()
 *                                              -> Returns a null terminated static const array of section_info objects
 *                                                 This method should be static and the value returned should
 *                                                 be alive for the entire program lifetime (or while datalib is doing work)
 *                                                 This may not be implemented if 'has_section=false'
 *
 *
 *          Inherithing from gta3::data_traits makes those methods optional:
 *
 *
 *              bool setbyline(Store, MainData, SectionInfo*, String)
 *                                              -> Sets the state of the MainData object based on the received line String
 *                                                 Notice this should check and set the state of the MainData object.
 *                                                 Return false on failure.
 *
 *              bool posread(Store)             -> After successfully reading the contents from a file into the specified store,
 *                                                 this traits method gets called, you can take a chance to post-process the readen data.
 *
 *
 *              static bool getline(Key, Value, String)
 *                                              -> Outputs a line into the specified string based on the data in the key-value pair
 *                                                  Returns false on failure.
 *
 *              static MergedList prewrite(MergedList)
 *                                              -> Before writing to the file the merged list (of dominant data) you can take a chance
 *                                                 to post-process the merged list.
 *
 *      }
 *
 */
template<class TraitsType, class ContainerType>
class data_store final : public datalib::data_store<ContainerType>
{
    public:
        using traits_type = TraitsType;
        static const bool has_sections = traits_type::has_sections;             // Whether this data store contains gta sections (e.g. OBJS, CARS, ..)
        static const bool per_line_section = traits_type::per_line_section;     // Whether the section is defined per-line (e.g. gta.dat)

    protected:
        traits_type mtraits;

    public:

        //
        //  Constructors
        //

        data_store() = default;

        data_store(const data_store& rhs) :
            datalib::data_store(rhs), mtraits(rhs.mtraits) {}

        data_store(data_store&& rhs) :
            datalib::data_store(std::move(rhs)), mtraits(std::move(rhs.mtraits)) {}

        //
        //  Assignment Operators
        //

        data_store& operator=(const data_store& rhs)
        {
            datalib::data_store::operator=(rhs);
            this->mtraits = rhs.mtraits;
        }

        data_store& operator=(data_store&& rhs)
        {
            datalib::data_store::operator=(std::move(rhs));
            this->mtraits = std::move(rhs.mtraits);
        }

        /*
         *  traits method
         *      Returns the traits object used by this store
         */
        traits_type& traits()
        {
            return this->mtraits;
        }

        /*
         *  insert method
         *      Inserts a line (in form of data) into this store based on it's 'section' (or nullptr if not sectioned data)
         *
         */

        // find a better name for this method
        template<class traits_type = TraitsType>
        typename std::enable_if<!traits_type::is_reversed_kv, bool>::type
        /* bool */ insert(const section_info* section, const std::string& line)
        {
            mapped_type value;
            if(traits_type::setbyline(*this, value, section, line))
            {
                key_type key = mtraits.key_from_value(value);
                map.emplace(std::move(key), std::move(value));
                return true;
            }
            return false;
        }

        // find a better name for this method
        template<class traits_type = TraitsType>
        typename std::enable_if<traits_type::is_reversed_kv, bool>::type
        /* bool */ insert(const section_info* section, const std::string& line)
        {
            key_type key;
            if(traits_type::setbyline(*this, key, section, line))
            {
                mapped_type value = mtraits.value_from_key(key);
                map.emplace(std::move(key), std::move(value));
                return true;
            }
            return false;
        }

        /*
         *  section_by_kv method
         *      Gets the section_info* pointer based on the received key-value pair
         *      Method only available if 'traits_type::has_sections=true'
         */

        template<class traits_type = TraitsType>
        typename std::enable_if<!traits_type::is_reversed_kv && traits_type::has_sections, const section_info*>::type
        static /* const section_info* */ section_by_kv(const key_type& key, const mapped_type& value)
        {
            return value.section();
        }
        template<class traits_type = TraitsType>
        typename std::enable_if<traits_type::is_reversed_kv && traits_type::has_sections, const section_info*>::type
        static /* const section_info* */ section_by_kv(const key_type& key, const mapped_type& value)
        {
            return key.section();
        }


        /*
         *  getline method
         *      Output a line based on the content of a key-value pair
         */
        static bool getline(const key_type& key, const mapped_type& value, std::string& line)
        {
            return traits_type::getline<data_store>(key, value, line);
        }

        /*
         *  load_from_file method
         *      Loads the content from the specified file ('arg1') into this data store
         *      --- THIS METHOD SHOULD BE IMPLEMENT FOR EVERY DERIVED BECAUSE OF datalib::data_store::load DEDUCTION ---
         */
        template<typename Arg1>
        bool load_from_file(Arg1&& arg1)
        {
            return this->load(*this, std::forward<Arg1>(arg1), parse_from_file());
        }

        /*
         *  sections method
         *      Gets an array of possible sections for the data set
         *      Method only available if 'traits_type::has_sections=true'
         */
        template<class traits_type = TraitsType>
        typename std::enable_if<traits_type::has_sections, const section_info*>::type
        static /* const section_info* */ sections()
        {
            return TraitsType::sections();
        }


        /*
         *  The following functions are called from the read/write/merge operations behind the scenes (from io.hpp)
         *  They should forward the call to the traits object.
         */

        // Called after successfully reading a file
        bool posread()
        {
            return mtraits.posread(*this);
        }

        // Called before writing the merge results into a file, to process the merged list.
        template<class MergedList>
        static MergedList& prewrite(MergedList& merged)
        {
            return traits_type::prewrite<data_store>(merged);
        }
};


/*
 *  traits_base
 *      An base trait for the gta3::data_store
 *      Implements some basic stuff which do not need to be implemented in the derived trait but can be overriden.
 *      Notice it doesn't implement everything required by gta3::data_store::traits_type! This is just a base!
 */
struct data_traits
{
    template<class StoreType>
    bool posread(StoreType&)
    {
        return true;
    }

    template<class StoreType, class MergedList>
    static MergedList& prewrite(MergedList& list)
    {
        return list;
    }


    template<class StoreType, typename TData>
    typename std::enable_if<StoreType::traits_type::has_sections, bool>::type
    static /* bool */ setbyline(StoreType& store, TData& data, const section_info* section, const std::string& line)
    {
        return (data.as_section(section, line) && data.set(line));
    }
    template<class StoreType, typename TData>
    typename std::enable_if<!StoreType::traits_type::has_sections, bool>::type
    static /* bool */ setbyline(StoreType& store, TData& data, const section_info* section, const std::string& line)
    {
        return (data.check(line) && data.set(line));
    }


    template<class StoreType, typename Key, typename Value>
    typename std::enable_if<!StoreType::traits_type::is_reversed_kv, bool>::type
    static /* bool */ getline(const Key& key, const Value& value, std::string& line)
    {
        return value.get(line);
    }
    template<class StoreType, typename Key, typename Value>
    typename std::enable_if<StoreType::traits_type::is_reversed_kv, bool>::type
    static /* bool */ getline(const Key& key, const Value& value, std::string& line)
    {
        return key.get(line);
    }

};


} // namespace gta3
} // namespace datalib
