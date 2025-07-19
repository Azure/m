// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <exception>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/utf/encode.h>
#include <m/utility/pointers.h>

using namespace std::string_view_literals;

namespace m
{
    namespace html_writer
    {
        using entity_map_type = std::map<char32_t, std::u8string>;

        //
        // This map is the default set of characters which when encountered
        // in a character data context will be converted into entity
        // references.
        //
        // Arbitrary entity maps may be supplied but then the caller
        // is responsible for ensuring that they are within some
        // combination of the standard set of entity references and
        // what the document's DTD supports, if it has a DTD.
        //
        static inline const entity_map_type default_encoding_map{{U'&', u8"&amp;"},
                                                                 {U'<', u8"&lt;"},
                                                                 {U'>', u8"&gt;"},
                                                                 {U'\'', u8"&apos;"},
                                                                 {U'"', u8"&quot;"}};
        struct suppress_envelope_t
        {
            explicit constexpr suppress_envelope_t() noexcept {}
        };

        static inline suppress_envelope_t suppress_envelope;

        /// <summary>
        /// A writer instance generally accept Unicode data in and
        /// writes out a UTF-8 stream.
        /// </summary>
        /// <typeparam name="OutputIteratorT"></typeparam>
        template <typename OutputIteratorT>
            requires(std::output_iterator<OutputIteratorT, char8_t>)
        class writer
        {
        public:
            // Perhaps in the future additional output character encodings
            // will be supported. At this moment in time, only the
            // UTF encoders built in to the m library are viable concerns
            // and nobody wants UTF-16 or UTF-32 HTML so we are only
            // targeting UTF-8 HTML.
            //
            using out_char_type        = char8_t;
            using output_iterator_type = OutputIteratorT;

            writer(OutputIteratorT&& outit, entity_map_type entity_map = default_encoding_map):
                m_outit(std::move(outit)), m_entity_map(entity_map), m_envelope_suppressed(false)
            {
                write_html_beginning();
            }

            writer(OutputIteratorT&& outit,
                   suppress_envelope_t,
                   entity_map_type entity_map = default_encoding_map):
                m_outit(std::move(outit)), m_entity_map(entity_map), m_envelope_suppressed(true)
            {
                //
            }

            writer(OutputIteratorT const& outit, entity_map_type entity_map = default_encoding_map):
                m_outit(outit), m_entity_map(entity_map), m_envelope_suppressed(false)
            {
                write_html_beginning();
            }

            ~writer()
            {
                if (!m_envelope_suppressed)
                    write_html_ending();
            }

            /// <summary>
            /// Normally, the destructor will write the ending </html> tag, but
            /// if you need to suppress this in, for example, a failure
            /// path, call suppress_envelope() and the destructor will not
            /// do this.
            ///
            /// The alternative was to make the success path have more code
            /// and it seems like it's more reasonable for the error path to
            /// suffer than for the success path. It's probably fine in most
            /// cases to just let it be written.
            /// </summary>
            void
            suppress_envelope() noexcept
            {
                m_envelope_suppressed = true;
            }

            template <typename InputIteratorT>
                requires(std::input_iterator<InputIteratorT> &&
                         std::is_same_v<std::iter_value_t<InputIteratorT>, char32_t>)
            void
            write_text_nomapping(InputIteratorT it, InputIteratorT end)
            {
                while (it != end)
                    m_outit = m::utf::encode_char(char8_t{}, *it++, m_outit);
            }

            template <typename CharT>
            void
            write_text_nomapping(std::basic_string_view<CharT> text)
            {
                write_text_nomapping(text.begin(), text.end());
            }

            class abstract_tag_writer
            {
            public:
                constexpr abstract_tag_writer(abstract_tag_writer&& other) noexcept:
                    m_writer{}, m_parent_tag_writer{}
                {
                    using std::swap;

                    swap(m_writer, other.m_writer);
                    swap(m_parent_tag_writer, other.m_parent_tag_writer);

                    if (m_writer->m_current_tag_writer == &other)
                        m_writer->m_current_tag_writer = this;
                }

                ~abstract_tag_writer()
                {
                    if (m_writer->m_current_tag_writer == this)
                    {
                        m_writer->m_current_tag_writer = m_parent_tag_writer;
                    }
                }

                friend void
                swap(abstract_tag_writer& l, abstract_tag_writer& r)
                {
                    using std::swap;

                    swap(l.m_writer, r.m_writer);
                    swap(l.m_parent_tag_writer, r.m_parent_tag_writer);
                }

            protected:
                constexpr abstract_tag_writer(m::not_null<writer*> w,
                                              abstract_tag_writer* parent_tag_writer) noexcept:
                    m_writer(w), m_parent_tag_writer(parent_tag_writer)
                {}
                writer*              m_writer;
                abstract_tag_writer* m_parent_tag_writer;
            };

            /// <summary>
            /// The open_tag_writer represents a tag that is in the process
            /// of being written but the right hand greater than has not been
            /// written yet.
            ///
            /// The open_tag_writer can have attributes added. Closing it
            /// yields a tag_writer.
            /// </summary>
            class open_tag_writer : protected abstract_tag_writer
            {
            public:
                constexpr open_tag_writer(open_tag_writer&& other) noexcept:
                    abstract_tag_writer(std::move(other))
                {}

                ///
                /// Writes an attribute for a tag as simply the attribute name with no
                /// equals sign or value after that. HTML permits this and it has the
                /// same semantics as setting the attribute equal to th empty string
                /// but in some settings it is aesthetically preferred.
                ///
                template <typename InputIteratorT>
                    requires(std::input_iterator<InputIteratorT> &&
                             std::is_same_v<std::iter_value_t<InputIteratorT>, char32_t>)
                void
                write_attribute(InputIteratorT it, InputIteratorT end)
                {
                    m_writer->write_text_nomapping(it, end);
                }

                template <typename NameInputIteratorT, typename ValueInputIteratorT>
                    requires(std::input_iterator<NameInputIteratorT> &&
                             std::is_same_v<std::iter_value_t<NameInputIteratorT>, char32_t> &&
                             std::input_iterator<ValueInputIteratorT> &&
                             std::is_same_v<std::iter_value_t<ValueInputIteratorT>, char32_t>)
                void
                write_attribute(NameInputIteratorT  name_it,
                                NameInputIteratorT  name_end,
                                ValueInputIteratorT value_it,
                                ValueInputIteratorT value_end)

                {
                    //
                }

                constexpr bool
                write_solidus() const
                {
                    return m_write_solidus;
                }

                constexpr bool
                write_solidus(bool v)
                {
                    // You can't change whether you are going to write > vs. /> after
                    // you've finished with the attributes.
                    //
                    M_INTERNAL_ERROR_CHECK(!m_done_attributes);
                    return std::exchange(m_write_solidus, v);
                }

                tag_writer
                close()
                {
                    M_INTERNAL_ERROR_CHECK(!m_done_attributes);
                    M_INTERNAL_ERROR_CHECK(m_writer->m_current_tag_writer == this);

                    if (m_write_solidus)
                        m_writer->write_text_nomapping(U"/>"sv);
                    else
                        m_writer->write_text_nomapping(U">"sv);

                    return tag_writer(m_writer, m_parent_tag_writer);
                }

                template <typename CharT>
                open_tag_writer
                open_tag(std::basic_string_view<CharT> name)
                {
                    M_INTERNAL_ERROR_CHECK(m_writer->m_current_tag_writer == this);

                    write_text_nomapping(U"<"sv);
                    write_text_nomapping(name);
                    return open_tag_writer(this);
                }

            protected:
                constexpr open_tag_writer(m::not_null<writer*> w,
                                          abstract_tag_writer* parent_tag_writer,
                                          bool                 write_solidus) noexcept:
                    abstract_tag_writer(w, parent_tag_writer),
                    m_write_solidus(write_solidus),
                    m_done_attributes(false)
                {
                    //
                }

                template <typename CharT>
                void
                write_text_nomapping_unchecked(std::basic_string_view<CharT> text)
                {
                    m_writer->write_text_nomapping(text.begin(), text.end());
                }

                bool m_write_solidus;
                bool m_done_attributes;

                friend class writer;
            };

            class tag_writer : protected abstract_tag_writer
            {
            public:
                constexpr tag_writer(tag_writer&& other) noexcept:
                    abstract_tag_writer(std::move(other))
                {
                }

            protected:
                constexpr tag_writer(m::not_null<writer*> w,
                                     abstract_tag_writer* parent_tag_writer) noexcept:
                    abstract_tag_writer(w, parent_tag_writer)
                {
                    //
                }
            };

            template <typename CharT>
            open_tag_writer
            open_tag(std::basic_string_view<CharT> name)
            {
                M_INTERNAL_ERROR_CHECK(!m_current_tag_writer);
                write_text_nomapping(U"<"sv);
                write_text_nomapping(name);
                return open_tag_writer(this);
            }

        protected:
            void
            write_html_beginning()
            {
                //
            }

            void
            write_html_ending()
            {
                //
            }

            OutputIteratorT  m_outit;
            entity_map_type  m_entity_map;
            open_tag_writer* m_current_tag_writer;
            bool             m_envelope_suppressed;
        };
    } // namespace html_writer
} // namespace m
