#pragma once

#include <Windows.h>

namespace utilities::tls {

        // Dynamic TLS wrapper for manual-mapped modules.
        // Static TLS (__declspec(thread) / thread_local) requires the Windows loader
        // to process the .tls section, which does not happen with manual mapping.
        // This wrapper uses TlsAlloc/TlsGetValue/TlsSetValue which work for any thread.

        template <typename T>
        class slot
        {
        public:
                slot( ) noexcept : m_index( TLS_OUT_OF_INDEXES ) { }

                ~slot( ) noexcept
                {
                        if ( m_index != TLS_OUT_OF_INDEXES )
                        {
                                TlsFree( m_index );
                        }
                }

                slot( const slot& ) = delete;
                slot& operator=( const slot& ) = delete;

                slot( slot&& other ) noexcept : m_index( other.m_index )
                {
                        other.m_index = TLS_OUT_OF_INDEXES;
                }

                slot& operator=( slot&& other ) noexcept
                {
                        if ( this != &other )
                        {
                                if ( m_index != TLS_OUT_OF_INDEXES )
                                {
                                        TlsFree( m_index );
                                }
                                m_index = other.m_index;
                                other.m_index = TLS_OUT_OF_INDEXES;
                        }
                        return *this;
                }

                [[nodiscard]] DWORD ensure( ) noexcept
                {
                        if ( m_index == TLS_OUT_OF_INDEXES )
                        {
                                m_index = TlsAlloc( );
                        }
                        return m_index;
                }

                [[nodiscard]] bool is_valid( ) const noexcept
                {
                        return m_index != TLS_OUT_OF_INDEXES;
                }

                [[nodiscard]] T* get( ) noexcept
                {
                        if ( m_index == TLS_OUT_OF_INDEXES )
                        {
                                return nullptr;
                        }
                        return static_cast<T*>( TlsGetValue( m_index ) );
                }

                [[nodiscard]] const T* get( ) const noexcept
                {
                        if ( m_index == TLS_OUT_OF_INDEXES )
                        {
                                return nullptr;
                        }
                        return static_cast<const T*>( TlsGetValue( m_index ) );
                }

                void set( T* value ) noexcept
                {
                        if ( m_index != TLS_OUT_OF_INDEXES )
                        {
                                TlsSetValue( m_index, value );
                        }
                }

        private:
                DWORD m_index;
        };

} // namespace utilities::tls
