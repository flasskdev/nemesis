#include <pch/pch.hpp>
#include <wincodec.h>
#pragma comment( lib, "windowscodecs.lib" )
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/game_path.hpp>
#include "../changer.hpp"

namespace features::changer {

	bool econ_item_system::initialize( )
	{
		std::uintptr_t schema{};
		auto schema_ready{ false };
		constexpr auto max_attempts{ 300 };
		constexpr auto retry_delay{ std::chrono::milliseconds( 100 ) };

		// Early injection can precede the schema's item-definition and paint-kit
		// tables. Wait until both are populated, then parse exactly once.
		for ( auto attempt = 0; attempt < max_attempts; ++attempt )
		{
			const auto system = memory::call<std::uintptr_t>( addresses::globals::item_system );
			schema = system
				? memory::safe_read<std::uintptr_t>( system + 0x8 ).value_or( 0 )
				: 0;

			if ( schema )
			{
				const auto item_count = memory::safe_read<int>( schema + 0x128 ).value_or( 0 );
				const auto item_array = memory::safe_read<std::uintptr_t>( schema + 0x130 ).value_or( 0 );
				const auto paint_count = memory::safe_read<int>( schema + 0x2F0 ).value_or( 0 );
				const auto paint_nodes = memory::safe_read<std::uintptr_t>( schema + 0x2F8 ).value_or( 0 );

				if ( item_count > 0 && item_count <= 10000 && item_array
					&& paint_count > 0 && paint_count <= 10000 && paint_nodes )
				{
					schema_ready = true;
					break;
				}
			}

			if ( attempt == 0 )
			{
				logging::console::print( xs( "[econ] waiting for item schema" ) );
			}

			std::this_thread::sleep_for( retry_delay );
		}

		if ( !schema_ready )
		{
			logging::console::print( xs( "[econ] timed out waiting for item schema" ) );
			return false;
		}

		if ( !this->parse_item_defs( schema ) )
		{
			logging::console::print( xs( "[econ] failed to parse item definitions" ) );
			return false;
		}

		if ( !this->parse_paint_kits( schema ) )
		{
			logging::console::print( xs( "[econ] failed to parse paint kits" ) );
			return false;
		}

		this->parse_music_kits( schema );

		this->build_indices( );
		this->resolve_localized_names( );

		if ( !this->build_vpk_index( ) )
		{
			logging::console::print( xs( "[econ] failed to build VPK index" ) );
			return false;
		}

		this->build_skin_index( );

		return true;
	}

	const econ_item_system::item_def* econ_item_system::find_def( std::int16_t def_index ) const
	{
		const auto it = this->m_def_index_map.find( def_index );
		if ( it == this->m_def_index_map.end( ) )
		{
			return nullptr;
		}

		return &this->m_item_defs[ it->second ];
	}

	const econ_item_system::paint_kit* econ_item_system::find_paint_kit( int id ) const
	{
		const auto it = this->m_paint_kit_map.find( id );
		if ( it == this->m_paint_kit_map.end( ) )
		{
			return nullptr;
		}

		return &this->m_paint_kits[ it->second ];
	}

	const econ_item_system::music_kit* econ_item_system::find_music_kit( int id ) const
	{
		const auto it = this->m_music_kit_map.find( id );
		if ( it == this->m_music_kit_map.end( ) )
		{
			return nullptr;
		}

		return &this->m_music_kits[ it->second ];
	}

	const econ_item_system::skin_image* econ_item_system::get_skin_image( const std::string& image_inventory )
	{
		if ( image_inventory.empty( ) )
		{
			return nullptr;
		}

		std::lock_guard lock( this->m_image_mutex );

		auto it = this->m_image_cache.find( image_inventory );
		if ( it == this->m_image_cache.end( ) )
		{
			auto entry = std::make_unique<image_entry>( );
			it = this->m_image_cache.emplace( image_inventory, std::move( entry ) ).first;
			this->request_decode( image_inventory );
			return nullptr;
		}

		const auto state = it->second->state.load( std::memory_order_acquire );
		if ( state == image_state::ready )
		{
			return &it->second->image;
		}

		if ( state == image_state::decoded )
		{
			if ( this->finalize_texture( *it->second ) )
			{
				return &it->second->image;
			}
		}

		return nullptr;
	}

	const econ_item_system::skin_image* econ_item_system::get_skin_image( std::int16_t def_index, int paint_kit_id )
	{
		const auto def = this->find_def( def_index );
		if ( !def )
		{
			return nullptr;
		}

		const auto pk = ( paint_kit_id != 0 ) ? this->find_paint_kit( paint_kit_id ) : nullptr;
		const auto path = this->build_skin_image_path( def, pk );

		return this->get_skin_image( path );
	}

	int econ_item_system::combined_rarity( std::int16_t def_index, int paint_kit_id ) const
	{
		auto weapon_rarity{ 0 };
		auto paint_rarity{ 0 };
		auto category{ item_category::other };

		if ( const auto def = this->find_def( def_index ) )
		{
			weapon_rarity = def->rarity;
			category = def->category;
		}

		if ( const auto pk = this->find_paint_kit( paint_kit_id ) )
		{
			paint_rarity = pk->rarity;
		}

		if ( category == item_category::knife )
		{
			return 5;
		}

		return std::clamp( weapon_rarity + paint_rarity - 2, 0, 7 );
	}

	void econ_item_system::flush_skin_images( )
	{
		{
			std::lock_guard lock( this->m_image_mutex );
			this->m_image_cache.clear( );
		}

		{
			std::lock_guard lock( this->m_vpk_mutex );
			this->m_archive_handles.clear( );
		}
	}

	bool econ_item_system::parse_item_defs( std::uintptr_t schema )
	{
		const auto count = memory::read<int>( schema + 0x128 );
		const auto array = memory::read<std::uintptr_t>( schema + 0x130 );

		if ( !array || count <= 0 || count > 10000 )
		{
			return false;
		}

		this->m_item_defs.reserve( count );

		for ( auto i = 0; i < count; i++ )
		{
			const auto entry_base = array + static_cast< std::uintptr_t >( 32 * i );
			const auto def_index = memory::read<int>( entry_base + 16 );

			if ( def_index < -1 )
			{
				continue;
			}

			const auto def_ptr = memory::read<std::uintptr_t>( entry_base + 24 );
			if ( !def_ptr )
			{
				continue;
			}

			item_def item{};
			item.def_index = memory::read<std::int16_t>( def_ptr + 0x10 );
			item.loadout_slot = memory::read<int>( def_ptr + 0x338 );
			item.used_by_classes = memory::read<std::uint32_t>( def_ptr + 0x368 );
			item.rarity = memory::read<std::uint8_t>( def_ptr + 0x42 );

			if ( const auto name_ptr = memory::read<std::uintptr_t>( def_ptr + 0x260 ); name_ptr )
			{
				item.name = memory::read_string( name_ptr );
				item.item_class = item.name;
			}

			if ( const auto model_ptr = memory::read<std::uintptr_t>( def_ptr + 0x148 ); model_ptr )
			{
				item.model_player = memory::read_string( model_ptr );
			}

			if ( const auto img_ptr = memory::read<std::uintptr_t>( def_ptr + 0xA8 ); img_ptr )
			{
				item.image_inventory = memory::read_string( img_ptr );
			}

			if ( const auto token_ptr = memory::read<std::uintptr_t>( def_ptr + 0x70 ); token_ptr )
			{
				const auto token = memory::read_string( token_ptr );
				const auto localized = memory::call_vfunc<const char*>( addresses::globals::localize, 17, token.c_str( ) );

				if ( localized && *localized && std::strcmp( localized, token.c_str( ) ) != 0 )
				{
					item.localized_name = localized;
				}
				else
				{
					item.localized_name = item.name;
				}
			}
			else
			{
				item.localized_name = item.name;
			}

			item.category = this->classify( item.item_class.c_str( ), item.loadout_slot );
			this->m_item_defs.push_back( std::move( item ) );
		}

		return !this->m_item_defs.empty( );
	}

	bool econ_item_system::parse_paint_kits( std::uintptr_t schema )
	{
		const auto tree_base = schema + 0x2F0;

		const auto count = memory::read<int>( tree_base + 0x00 );
		const auto nodes = memory::read<std::uintptr_t>( tree_base + 0x08 );

		if ( !nodes || count <= 0 || count > 10000 )
		{
			return false;
		}

		this->m_paint_kits.reserve( count );

		for ( auto i = 0; i < count; i++ )
		{
			const auto node_base = nodes + static_cast< std::uintptr_t >( 32 * i );
			const auto pk_ptr = memory::read<std::uintptr_t>( node_base + 24 );

			if ( !pk_ptr )
			{
				continue;
			}

			paint_kit pk{};
			pk.id = memory::read<int>( pk_ptr + 0x00 );
			pk.wear_min = memory::read<float>( pk_ptr + 0x6C );
			pk.wear_max = memory::read<float>( pk_ptr + 0x70 );
			pk.legacy_model = memory::read<bool>( pk_ptr + 0xAE );
			pk.rarity = static_cast< std::uint8_t >( memory::read<int>( pk_ptr + 0x44 ) & 0xFF );

			if ( const auto name_ptr = memory::read<std::uintptr_t>( pk_ptr + 0x08 ); name_ptr )
			{
				pk.name = memory::read_string( name_ptr );
			}

			if ( const auto desc_ptr = memory::read<std::uintptr_t>( pk_ptr + 0x10 ); desc_ptr )
			{
				pk.desc_token = memory::read_string( desc_ptr );
			}

			if ( const auto name_token_ptr = memory::read<std::uintptr_t>( pk_ptr + 0x18 ); name_token_ptr )
			{
				pk.name_token = memory::read_string( name_token_ptr );
			}

			this->m_paint_kits.push_back( std::move( pk ) );
		}

		return !this->m_paint_kits.empty( );
	}

	namespace {
		bool decode_image_wic(
			const std::uint8_t* compressed_data,
			std::size_t compressed_size,
			std::vector<std::uint8_t>& out_pixels,
			std::uint32_t& out_w,
			std::uint32_t& out_h )
		{
			const auto hr_init = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
			const bool uninit_needed = SUCCEEDED( hr_init );

			auto cleanup = [ & ]( )
			{
				if ( uninit_needed )
				{
					CoUninitialize( );
				}
			};

			Microsoft::WRL::ComPtr<IWICImagingFactory> factory{};
			if ( FAILED( CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ) ) || !factory )
			{
				cleanup( );
				return false;
			}

			Microsoft::WRL::ComPtr<IWICStream> stream{};
			if ( FAILED( factory->CreateStream( &stream ) ) )
			{
				cleanup( );
				return false;
			}

			if ( FAILED( stream->InitializeFromMemory( const_cast< BYTE* >( compressed_data ), static_cast< DWORD >( compressed_size ) ) ) )
			{
				cleanup( );
				return false;
			}

			Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder{};
			if ( FAILED( factory->CreateDecoderFromStream( stream.Get( ), nullptr, WICDecodeMetadataCacheOnDemand, &decoder ) ) )
			{
				cleanup( );
				return false;
			}

			Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame{};
			if ( FAILED( decoder->GetFrame( 0, &frame ) ) )
			{
				cleanup( );
				return false;
			}

			std::uint32_t width{}, height{};
			if ( FAILED( frame->GetSize( &width, &height ) ) || width == 0 || height == 0 )
			{
				cleanup( );
				return false;
			}

			Microsoft::WRL::ComPtr<IWICFormatConverter> converter{};
			if ( FAILED( factory->CreateFormatConverter( &converter ) ) )
			{
				cleanup( );
				return false;
			}

			if ( FAILED( converter->Initialize( frame.Get( ), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom ) ) )
			{
				cleanup( );
				return false;
			}

			out_pixels.resize( static_cast< std::size_t >( width ) * height * 4 );
			if ( FAILED( converter->CopyPixels( nullptr, width * 4, static_cast< std::uint32_t >( out_pixels.size( ) ), out_pixels.data( ) ) ) )
			{
				out_pixels.clear( );
				cleanup( );
				return false;
			}

			out_w = width;
			out_h = height;
			cleanup( );
			return true;
		}

// Auto-generated 101 CS2 Music Kits from items_game.txt + csgo_english.txt
struct fallback_music_kit
{
	int id;
	const char* name;
	const char* loc_name;
	const char* loc_desc;
	const char* image_inventory;
	const char* fallback_title;
	std::uint8_t rarity;
};

static constexpr fallback_music_kit k_fallback_kits[] = {
	{ 1, "valve_cs2_01", "#musickit_valve_cs2_01", "#musickit_valve_cs2_01_desc", "econ/music_kits/valve_cs2_01", "Valve, Counter-Strike 2", 3 },
	{ 2, "valve_02", "#musickit_valve_csgo_02", "#musickit_valve_csgo_02_desc", "econ/music_kits/valve_02", "valve_02", 3 },
	{ 3, "danielsadowski_01", "#musickit_danielsadowski_01", "#musickit_danielsadowski_01_desc", "econ/music_kits/danielsadowski_01", "Daniel Sadowski, Crimson Assault", 3 },
	{ 4, "noisia_01", "#musickit_noisia_01", "#musickit_noisia_01_desc", "econ/music_kits/noisia_01", "Noisia, Sharpened", 3 },
	{ 5, "robertallaire_01", "#musickit_robertallaire_01", "#musickit_robertallaire_01_desc", "econ/music_kits/robertallaire_01", "Robert Allaire, Insurgency", 3 },
	{ 6, "seanmurray_01", "#musickit_seanmurray_01", "#musickit_seanmurray_01_desc", "econ/music_kits/seanmurray_01", "Sean Murray, A*D*8", 3 },
	{ 7, "feedme_01", "#musickit_feedme_01", "#musickit_feedme_01_desc", "econ/music_kits/feedme_01", "Feed Me, High Noon", 3 },
	{ 8, "dren_01", "#musickit_dren_01", "#musickit_dren_01_desc", "econ/music_kits/dren_01", "Dren, Death's Head Demolition", 3 },
	{ 9, "austinwintory_01", "#musickit_austinwintory_01", "#musickit_austinwintory_01_desc", "econ/music_kits/austinwintory_01", "Austin Wintory, Desert Fire", 3 },
	{ 10, "sasha_01", "#musickit_sasha_01", "#musickit_sasha_01_desc", "econ/music_kits/sasha_01", "Sasha, LNOE", 3 },
	{ 11, "skog_01", "#musickit_skog_01", "#musickit_skog_01_desc", "econ/music_kits/skog_01", "Skog, Metal", 3 },
	{ 12, "midnightriders_01", "#musickit_midnightriders_01", "#musickit_midnightriders_01_desc", "econ/music_kits/midnightriders_01", "Midnight Riders, All I Want for Christmas", 3 },
	{ 13, "mattlange_01", "#musickit_mattlange_01", "#musickit_mattlange_01_desc", "econ/music_kits/mattlange_01", "Matt Lange, IsoRhythm", 3 },
	{ 14, "mateomessina_01", "#musickit_mateomessina_01", "#musickit_mateomessina_01_desc", "econ/music_kits/mateomessina_01", "Mateo Messina, For No Mankind", 3 },
	{ 15, "hotlinemiami_01", "#musickit_hotlinemiami_01", "#musickit_hotlinemiami_01_desc", "econ/music_kits/hotlinemiami_01", "Various Artists, Hotline Miami", 3 },
	{ 16, "danielsadowski_02", "#musickit_danielsadowski_02", "#musickit_danielsadowski_02_desc", "econ/music_kits/danielsadowski_02", "Daniel Sadowski, Total Domination", 3 },
	{ 17, "damjanmravunac_01", "#musickit_damjanmravunac_01", "#musickit_damjanmravunac_01_desc", "econ/music_kits/damjanmravunac_01", "Damjan Mravunac, The Talos Principle", 3 },
	{ 18, "proxy_01", "#musickit_proxy_01", "#musickit_proxy_01_desc", "econ/music_kits/proxy_01", "Proxy, Battlepack", 3 },
	{ 19, "kitheory_01", "#musickit_kitheory_01", "#musickit_kitheory_01_desc", "econ/music_kits/kitheory_01", "Ki:Theory, MOLOTOV", 3 },
	{ 20, "troelsfolmann_01", "#musickit_troelsfolmann_01", "#musickit_troelsfolmann_01_desc", "econ/music_kits/troelsfolmann_01", "Troels Folmann, Uber Blasto Phone", 3 },
	{ 21, "kellybailey_01", "#musickit_kellybailey_01", "#musickit_kellybailey_01_desc", "econ/music_kits/kellybailey_01", "Kelly Bailey, Hazardous Environments", 3 },
	{ 22, "skog_02", "#musickit_skog_02", "#musickit_skog_02_desc", "econ/music_kits/skog_02", "Skog, II-Headshot", 3 },
	{ 23, "danielsadowski_03", "#musickit_danielsadowski_03", "#musickit_danielsadowski_03_desc", "econ/music_kits/danielsadowski_03", "Daniel Sadowski, The 8-Bit Kit", 3 },
	{ 24, "awolnation_01", "#musickit_awolnation_01", "#musickit_awolnation_01_desc", "econ/music_kits/awolnation_01", "AWOLNATION, I Am", 3 },
	{ 25, "mordfustang_01", "#musickit_mordfustang_01", "#musickit_mordfustang_01_desc", "econ/music_kits/mordfustang_01", "Mord Fustang, Diamonds", 3 },
	{ 26, "michaelbross_01", "#musickit_michaelbross_01", "#musickit_michaelbross_01_desc", "econ/music_kits/michaelbross_01", "Michael Bross, Invasion!", 3 },
	{ 27, "ianhultquist_01", "#musickit_ianhultquist_01", "#musickit_ianhultquist_01_desc", "econ/music_kits/ianhultquist_01", "Ian Hultquist, Lion's Mouth", 3 },
	{ 28, "newbeatfund_01", "#musickit_newbeatfund_01", "#musickit_newbeatfund_01_desc", "econ/music_kits/newbeatfund_01", "New Beat Fund, Sponge Fingerz", 3 },
	{ 29, "beartooth_01", "#musickit_beartooth_01", "#musickit_beartooth_01_desc", "econ/music_kits/beartooth_01", "Beartooth, Disgusting", 3 },
	{ 30, "lenniemoore_01", "#musickit_lenniemoore_01", "#musickit_lenniemoore_01_desc", "econ/music_kits/lenniemoore_01", "Lennie Moore, Java Havana Funkaloo", 3 },
	{ 31, "darude_01", "#musickit_darude_01", "#musickit_darude_01_desc", "econ/music_kits/darude_01", "Darude, Moments CSGO", 3 },
	{ 32, "beartooth_02", "#musickit_beartooth_02", "#musickit_beartooth_02_desc", "econ/music_kits/beartooth_02", "Beartooth, Aggressive", 3 },
	{ 33, "blitzkids_01", "#musickit_blitzkids_01", "#musickit_blitzkids_01_desc", "econ/music_kits/blitzkids_01", "Blitz Kids, The Good Youth", 3 },
	{ 34, "hundredth_01", "#musickit_hundredth_01", "#musickit_hundredth_01_desc", "econ/music_kits/hundredth_01", "Hundredth, FREE", 3 },
	{ 35, "neckdeep_01", "#musickit_neckdeep_01", "#musickit_neckdeep_01_desc", "econ/music_kits/neckdeep_01", "Neck Deep, Life's Not Out To Get You", 3 },
	{ 36, "roam_01", "#musickit_roam_01", "#musickit_roam_01_desc", "econ/music_kits/roam_01", "Roam, Backbone", 3 },
	{ 37, "twinatlantic_01", "#musickit_twinatlantic_01", "#musickit_twinatlantic_01_desc", "econ/music_kits/twinatlantic_01", "Twin Atlantic, GLA", 3 },
	{ 38, "skog_03", "#musickit_skog_03", "#musickit_skog_03_desc", "econ/music_kits/skog_03", "Skog, III-Arena", 3 },
	{ 39, "theverkkars_01", "#musickit_theverkkars_01", "#musickit_theverkkars_01_desc", "econ/music_kits/theverkkars_01", "The Verkkars, EZ4ENCE", 3 },
	{ 40, "halo_01", "#musickit_halo_01", "#musickit_halo_01_desc", "econ/music_kits/halo_01", "Halo, The Master Chief Collection", 3 },
	{ 41, "scarlxrd_01", "#musickit_scarlxrd_01", "#musickit_scarlxrd_01_desc", "econ/music_kits/scarlxrd_01", "Scarlxrd: King, Scar", 3 },
	{ 42, "hlalyx_01", "#musickit_hlalyx_01", "#musickit_hlalyx_01_desc", "econ/music_kits/hlalyx_01", "Half-Life: Alyx, Anti-Citizen", 3 },
	{ 43, "austinwintory_02", "#musickit_austinwintory_02", "#musickit_austinwintory_02_desc", "econ/music_kits/austinwintory_02", "Austin Wintory, Bachram", 3 },
	{ 44, "dren_02", "#musickit_dren_02", "#musickit_dren_02_desc", "econ/music_kits/dren_02", "Dren, Gunman Taco Truck", 3 },
	{ 45, "danielsadowski_04", "#musickit_danielsadowski_04", "#musickit_danielsadowski_04_desc", "econ/music_kits/danielsadowski_04", "Daniel Sadowski, Eye of the Dragon", 3 },
	{ 46, "treeadams_benbromfield_01", "#musickit_treeadams_benbromfield_01", "#musickit_treeadams_benbromfield_01_desc", "econ/music_kits/treeadams_benbromfield_01", "Tree Adams and Ben Bromfield, M.U.D.D. FORCE", 3 },
	{ 47, "timhuling_01", "#musickit_timhuling_01", "#musickit_timhuling_01_desc", "econ/music_kits/timhuling_01", "Tim Huling, Neo Noir", 3 },
	{ 48, "sammarshall_01", "#musickit_sammarshall_01", "#musickit_sammarshall_01_desc", "econ/music_kits/sammarshall_01", "Sam Marshall, Bodacious", 3 },
	{ 49, "mattlevine_01", "#musickit_mattlevine_01", "#musickit_mattlevine_01_desc", "econ/music_kits/mattlevine_01", "Matt Levine, Drifter", 3 },
	{ 50, "amontobin_01", "#musickit_amontobin_01", "#musickit_amontobin_01_desc", "econ/music_kits/amontobin_01", "Amon Tobin, All for Dust", 3 },
	{ 51, "hades_01", "#musickit_hades_01", "#musickit_hades_01_desc", "econ/music_kits/hades_01", "Darren Korb, Hades Music Kit", 3 },
	{ 52, "neckdeep_02", "#musickit_neckdeep_02", "#musickit_neckdeep_02_desc", "econ/music_kits/neckdeep_02", "Neck Deep, The Lowlife Pack", 3 },
	{ 53, "scarlxrd_02", "#musickit_scarlxrd_02", "#musickit_scarlxrd_02_desc", "econ/music_kits/scarlxrd_02", "Scarlxrd, CHAIN$AW.LXADXUT.", 3 },
	{ 54, "austinwintory_03", "#musickit_austinwintory_03", "#musickit_austinwintory_03_desc", "econ/music_kits/austinwintory_03", "Austin Wintory, Mocha Petal", 3 },
	{ 55, "chipzel_01", "#musickit_chipzel_01", "#musickit_chipzel_01_desc", "econ/music_kits/chipzel_01", "Chipzel, Yellow Magic", 3 },
	{ 56, "freakydna_01", "#musickit_freakydna_01", "#musickit_freakydna_01_desc", "econ/music_kits/freakydna_01", "Freaky DNA, Vici", 3 },
	{ 57, "jesseharlin_01", "#musickit_jesseharlin_01", "#musickit_jesseharlin_01_desc", "econ/music_kits/jesseharlin_01", "Jesse Harlin, Astro Bellum", 3 },
	{ 58, "laurashigihara_01", "#musickit_laurashigihara_01", "#musickit_laurashigihara_01_desc", "econ/music_kits/laurashigihara_01", "Laura Shigihara: Work Hard, Play Hard", 3 },
	{ 59, "sarahschachner_01", "#musickit_sarahschachner_01", "#musickit_sarahschachner_01_desc", "econ/music_kits/sarahschachner_01", "Sarah Schachner, KOLIBRI", 3 },
	{ 60, "bbnos_01", "#musickit_bbnos_01", "#musickit_bbnos_01_desc", "econ/music_kits/bbnos_01", "bbno$, u mad!", 3 },
	{ 61, "theverkkars_02", "#musickit_theverkkars_02", "#musickit_theverkkars_02_desc", "econ/music_kits/theverkkars_02", "The Verkkars & n0thing, Flashbang Dance", 3 },
	{ 62, "3kliksphilip_01", "#musickit_3kliksphilip_01", "#musickit_3kliksphilip_01_desc", "econ/music_kits/3kliksphilip_01", "3kliksphilip, Heading for the Source", 3 },
	{ 63, "hlb_01", "#musickit_hlb_01", "#musickit_hlb_01_desc", "econ/music_kits/hlb_01", "Humanity's Last Breath, Void", 3 },
	{ 64, "juelz_01", "#musickit_juelz_01", "#musickit_juelz_01_desc", "econ/music_kits/juelz_01", "Juelz, Shooters", 3 },
	{ 65, "knock2_01", "#musickit_knock2_01", "#musickit_knock2_01_desc", "econ/music_kits/knock2_01", "Knock2, dashstar*", 3 },
	{ 66, "meechydarko_01", "#musickit_meechydarko_01", "#musickit_meechydarko_01_desc", "econ/music_kits/meechydarko_01", "Meechy Darko, Gothic Luxury", 3 },
	{ 67, "sullivanking_01", "#musickit_sullivanking_01", "#musickit_sullivanking_01_desc", "econ/music_kits/sullivanking_01", "Sullivan King, Lock Me Up", 3 },
	{ 68, "perfectworld_01", "#musickit_perfectworld_01", "#musickit_perfectworld_01_desc", "econ/music_kits/perfectworld_01", "Perfect World, Hua Lian (Painted Face)", 3 },
	{ 69, "denzelcurry_01", "#musickit_denzelcurry_01", "#musickit_denzelcurry_01_desc", "econ/music_kits/denzelcurry_01", "Denzel Curry, ULTIMATE", 3 },
	{ 70, "valve_01", "#musickit_valve_csgo_01", "#musickit_valve_csgo_01_desc", "econ/music_kits/valve_01", "Valve, CS:GO", 3 },
	{ 71, "dryden_01", "#musickit_dryden_01", "#musickit_dryden_01_desc", "econ/music_kits/dryden_01", "DRYDEN, Feel The Power", 3 },
	{ 72, "isoxo_01", "#musickit_isoxo_01", "#musickit_isoxo_01_desc", "econ/music_kits/isoxo_01", "ISOxo, inhuman", 3 },
	{ 73, "killscript_01", "#musickit_killscript_01", "#musickit_killscript_01_desc", "econ/music_kits/killscript_01", "KILL SCRIPT, All Night", 3 },
	{ 74, "knock2_02", "#musickit_knock2_02", "#musickit_knock2_02_desc", "econ/music_kits/knock2_02", "Knock2, Make U SWEAT!", 3 },
	{ 75, "radcat_01", "#musickit_radcat_01", "#musickit_radcat_01_desc", "econ/music_kits/radcat_01", "Rad Cat, Reason", 3 },
	{ 76, "twerl_01", "#musickit_twerl_01", "#musickit_twerl_01_desc", "econ/music_kits/twerl_01", "TWERL and Ekko & Sidetrack, Under Bright Lights", 3 },
	{ 78, "austinwintory_04", "#MusicKit_austinwintory_04", "#MusicKit_austinwintory_04_desc", "econ/music_kits/austinwintory_04", "Austin Wintory, The Devil Went Clubbing in Georgia", 3 },
	{ 79, "benbromfield_01", "#MusicKit_benbromfield_01", "#MusicKit_benbromfield_01_desc", "econ/music_kits/benbromfield_01", "Ben Bromfield, Rabbit Hole", 3 },
	{ 80, "danielsadowski_05", "#MusicKit_danielsadowski_05", "#MusicKit_danielsadowski_05_desc", "econ/music_kits/danielsadowski_05", "Daniel Sadowski, Dead Shot", 3 },
	{ 81, "dren_03", "#MusicKit_dren_03", "#MusicKit_dren_03_desc", "econ/music_kits/dren_03", "Dren McDonald, Coffee! Kofe! Kahveh!", 3 },
	{ 82, "mattlevine_02", "#MusicKit_mattlevine_02", "#MusicKit_mattlevine_02_desc", "econ/music_kits/mattlevine_02", "Matt Levine, Agency", 3 },
	{ 83, "sammarshall_02", "#MusicKit_sammarshall_02", "#MusicKit_sammarshall_02_desc", "econ/music_kits/sammarshall_02", "Sam Marshall, Clutch", 3 },
	{ 84, "timhuling_02", "#MusicKit_timhuling_02", "#MusicKit_timhuling_02_desc", "econ/music_kits/timhuling_02", "Tim Huling, Devil's Paintbrush", 3 },
	{ 85, "treeadams_01", "#MusicKit_treeadams_01", "#MusicKit_treeadams_01_desc", "econ/music_kits/treeadams_01", "Tree Adams, Seventh Moon", 3 },
	{ 86, "perfectworld_02", "#MusicKit_perfectworld_02", "#MusicKit_perfectworld_02_desc", "econ/music_kits/perfectworld_02", "Perfect World, Ay Hey", 3 },
	{ 87, "adambeyer_01", "#MusicKit_adambeyer_01", "#MusicKit_adambeyer_01_desc", "econ/music_kits/adambeyer_01", "Adam Beyer, Red Room", 3 },
	{ 88, "ghost_01", "#MusicKit_ghost_01", "#MusicKit_ghost_01_desc", "econ/music_kits/ghost_01", "Ghost, Skeleta", 3 },
	{ 89, "health_01", "#MusicKit_health_01", "#MusicKit_health_01_desc", "econ/music_kits/health_01", "HEALTH, RAT WARS", 3 },
	{ 90, "jamesandthecoldgun_01", "#MusicKit_jamesandthecoldgun_01", "#MusicKit_jamesandthecoldgun_01_desc", "econ/music_kits/jamesandthecoldgun_01", "James and the Cold Gun, Chewing Glass", 3 },
	{ 91, "jonathanyoung_01", "#MusicKit_jonathanyoung_01", "#MusicKit_jonathanyoung_01_desc", "econ/music_kits/jonathanyoung_01", "Jonathan Young, Starship Velociraptor", 3 },
	{ 92, "juelz_02", "#MusicKit_juelz_02", "#MusicKit_juelz_02_desc", "econ/music_kits/juelz_02", "Juelz, Floorspace", 3 },
	{ 93, "killermike_01", "#MusicKit_killermike_01", "#MusicKit_killermike_01_desc", "econ/music_kits/killermike_01", "Killer Mike, MICHAEL", 3 },
	{ 94, "pvris_01", "#MusicKit_pvris_01", "#MusicKit_pvris_01_desc", "econ/music_kits/pvris_01", "PVRIS, Evergreen", 3 },
	{ 95, "selectiveresponse_01", "#MusicKit_selectiveresponse_01", "#MusicKit_selectiveresponse_01_desc", "econ/music_kits/selectiveresponse_01", "Selective Response, No Love Only Pleasure", 3 },
	{ 96, "tigercub_01", "#MusicKit_tigercub_01", "#MusicKit_tigercub_01_desc", "econ/music_kits/tigercub_01", "Tigercub, The Perfume of Decay", 3 },
	{ 98, "alrt_01", "#MusicKit_alrt_01", "#MusicKit_alrt_01_desc", "econ/music_kits/alrt_01", "ALRT, DOPAMINE HIT", 3 },
	{ 99, "altare_01", "#MusicKit_altare_01", "#MusicKit_altare_01_desc", "econ/music_kits/altare_01", "Altare, Change My Mind", 3 },
	{ 100, "borne_01", "#MusicKit_borne_01", "#MusicKit_borne_01_desc", "econ/music_kits/borne_01", "borne, Give It To Me", 3 },
	{ 101, "pirapus_01", "#MusicKit_pirapus_01", "#MusicKit_pirapus_01_desc", "econ/music_kits/pirapus_01", "Pirapus, EVERYNITE", 3 },
	{ 102, "repiet_01", "#MusicKit_repiet_01", "#MusicKit_repiet_01_desc", "econ/music_kits/repiet_01", "Repiet & Julia Kleijn, On And On", 3 },
	{ 103, "shockone_01", "#MusicKit_shockone_01", "#MusicKit_shockone_01_desc", "econ/music_kits/shockone_01", "ShockOne, Voices", 3 },
};
	} // namespace

	bool econ_item_system::parse_music_kits( std::uintptr_t /*schema*/ )
	{
		this->m_music_kits.clear( );
		this->m_music_kits.reserve( sizeof( k_fallback_kits ) / sizeof( k_fallback_kits[ 0 ] ) );

		for ( const auto& fb : k_fallback_kits )
		{
			music_kit mk{};
			mk.id = fb.id;
			mk.name = fb.name;
			mk.loc_name = fb.loc_name;
			mk.loc_desc = fb.loc_desc;
			mk.image_inventory = fb.image_inventory;
			mk.rarity = fb.rarity;
			mk.localized_name = fb.fallback_title;

			this->m_music_kits.push_back( std::move( mk ) );
		}

		return !this->m_music_kits.empty( );
	}

	void econ_item_system::build_indices( )
	{
		for ( auto i = 0ull; i < this->m_item_defs.size( ); i++ )
		{
			const auto& def = this->m_item_defs[ i ];
			this->m_def_index_map[ def.def_index ] = i;

			switch ( def.category )
			{
			case item_category::knife:
				this->m_knives.push_back( &def );
				break;
			case item_category::glove:
				this->m_gloves.push_back( &def );
				break;
			case item_category::agent:
				this->m_agents.push_back( &def );
				break;
			case item_category::gun:
				this->m_guns.push_back( &def );
				break;
			default:
				break;
			}
		}

		for ( auto i = 0ull; i < this->m_paint_kits.size( ); i++ )
		{
			this->m_paint_kit_map[ this->m_paint_kits[ i ].id ] = i;
		}

		for ( auto i = 0ull; i < this->m_music_kits.size( ); i++ )
		{
			this->m_music_kit_map[ this->m_music_kits[ i ].id ] = i;
		}
	}

	void econ_item_system::resolve_localized_names( )
	{
		auto resolved{ 0 };
		auto fallback{ 0 };

		for ( auto& pk : this->m_paint_kits )
		{
			if ( !pk.name_token.empty( ) )
			{
				const auto localized = memory::call_vfunc<const char*>( addresses::globals::localize, 17, pk.name_token.c_str( ) );
				if ( localized && *localized && std::strcmp( localized, pk.name_token.c_str( ) ) != 0 )
				{
					pk.localized_name = localized;
					resolved++;
					continue;
				}
			}

			pk.localized_name = pk.name;
			fallback++;
		}

		for ( auto& mk : this->m_music_kits )
		{
			if ( addresses::globals::localize && !mk.loc_name.empty( ) )
			{
				const auto localized = memory::call_vfunc<const char*>( addresses::globals::localize, 17, mk.loc_name.c_str( ) );
				if ( localized && *localized && std::strcmp( localized, mk.loc_name.c_str( ) ) != 0 )
				{
					mk.localized_name = localized;
				}
			}

			if ( mk.localized_name.empty( ) )
			{
				mk.localized_name = mk.name;
			}

			if ( addresses::globals::localize && !mk.loc_desc.empty( ) )
			{
				const auto localized_desc = memory::call_vfunc<const char*>( addresses::globals::localize, 17, mk.loc_desc.c_str( ) );
				if ( localized_desc && *localized_desc && std::strcmp( localized_desc, mk.loc_desc.c_str( ) ) != 0 )
				{
					mk.localized_desc = localized_desc;
				}
			}
		}
	}

	bool econ_item_system::build_vpk_index( )
	{
		if ( this->m_vpk_indexed )
		{
			return !this->m_vpk_index.empty( );
		}

		this->m_vpk_indexed = true;

		const auto csgo_directory = game_path::csgo_directory( );
		if ( !csgo_directory )
		{
			return false;
		}

		std::ifstream file( *csgo_directory / L"pak01_dir.vpk", std::ios::binary );

		if ( !file.is_open( ) )
		{
			return false;
		}

		this->m_vpk_directory = *csgo_directory;

#pragma pack( push, 1 )
		struct vpk_header
		{
			std::uint32_t signature;
			std::uint32_t version;
			std::uint32_t tree_size;
			std::uint32_t file_data_section_size;
			std::uint32_t archive_md5_section_size;
			std::uint32_t other_md5_section_size;
			std::uint32_t signature_section_size;
		};

		struct vpk_entry
		{
			std::uint32_t crc;
			std::uint16_t preload_bytes;
			std::uint16_t archive_index;
			std::uint32_t entry_offset;
			std::uint32_t entry_length;
			std::uint16_t terminator;
		};
#pragma pack( pop )

		vpk_header header{};
		file.read( reinterpret_cast< char* >( &header ), sizeof( header ) );

		if ( header.signature != 0x55AA1234 || header.version != 2 )
		{
			return false;
		}

		const auto tree_end = static_cast< std::streamoff >( sizeof( vpk_header ) ) + static_cast< std::streamoff >( header.tree_size );

		while ( file.tellg( ) < tree_end )
		{
			std::string extension;
			std::getline( file, extension, '\0' );

			if ( extension.empty( ) )
			{
				break;
			}

			const auto is_vtex = extension == xs( "vtex_c" );

			while ( true )
			{
				std::string dir_path;
				std::getline( file, dir_path, '\0' );

				if ( dir_path.empty( ) )
				{
					break;
				}

				const auto is_econ = is_vtex && dir_path.find( xs( "panorama/images/econ" ) ) != std::string::npos;

				while ( true )
				{
					std::string filename;
					std::getline( file, filename, '\0' );

					if ( filename.empty( ) )
					{
						break;
					}

					vpk_entry entry{};
					file.read( reinterpret_cast< char* >( &entry ), sizeof( entry ) );

					if ( entry.preload_bytes > 0 )
					{
						file.seekg( entry.preload_bytes, std::ios::cur );
					}

					if ( !is_econ )
					{
						continue;
					}

					constexpr auto prefix_len = std::string_view( "panorama/images/" ).size( );
					auto key = dir_path.substr( prefix_len ) + "/" + filename;

					this->m_vpk_index[ std::move( key ) ] = vpk_file_entry
					{
						entry.archive_index,
						entry.entry_offset,
						entry.entry_length
					};
				}
			}
		}

		return !this->m_vpk_index.empty( );
	}

	void econ_item_system::build_skin_index( )
	{
		std::unordered_map<std::string, int> pk_by_name;
		pk_by_name.reserve( this->m_paint_kits.size( ) );

		for ( const auto& pk : this->m_paint_kits )
		{
			pk_by_name.emplace( pk.name, pk.id );
		}

		std::unordered_map<std::string, std::int16_t> def_by_name;
		def_by_name.reserve( this->m_item_defs.size( ) );

		for ( const auto& d : this->m_item_defs )
		{
			if ( !d.name.empty( ) )
			{
				def_by_name.emplace( d.name, d.def_index );
			}
		}

		constexpr std::string_view prefix{ "econ/default_generated/" };
		constexpr std::string_view suffix{ "_light_png" };

		for ( const auto& [path, _] : this->m_vpk_index )
		{
			if ( !path.starts_with( prefix ) || !path.ends_with( suffix ) )
			{
				continue;
			}

			std::string_view stem( path );
			stem.remove_prefix( prefix.size( ) );
			stem.remove_suffix( suffix.size( ) );

			std::int16_t def_idx{ -1 };
			std::string_view pk_name;

			for ( auto i = stem.find( '_', 1 ); i != std::string_view::npos; i = stem.find( '_', i + 1 ) )
			{
				const auto candidate = std::string( stem.substr( 0, i ) );
				const auto it = def_by_name.find( candidate );

				if ( it == def_by_name.end( ) )
				{
					continue;
				}

				def_idx = it->second;
				pk_name = stem.substr( i + 1 );
			}

			if ( def_idx == -1 || pk_name.empty( ) )
			{
				continue;
			}

			const auto pk_it = pk_by_name.find( std::string( pk_name ) );
			if ( pk_it == pk_by_name.end( ) )
			{
				continue;
			}

			this->m_skins.push_back( { def_idx, pk_it->second } );
		}
	}

	void econ_item_system::request_decode( const std::string& image_inventory )
	{
		const auto key = image_inventory + xs( "_png" );

		std::vector<std::byte> data;
		{
			std::lock_guard lock( this->m_vpk_mutex );
			data = this->read_vpk( key );
		}

		if ( data.empty( ) )
		{
			this->m_image_cache[ image_inventory ]->state.store( image_state::failed, std::memory_order_release );
			return;
		}

		this->m_image_cache[ image_inventory ]->state.store( image_state::loading, std::memory_order_release );

		threadpool::run( [ this, inv = image_inventory, buf = std::move( data ) ]( )
			{
				std::lock_guard lock( this->m_image_mutex );

				const auto it = this->m_image_cache.find( inv );
				if ( it == this->m_image_cache.end( ) )
				{
					return;
				}

				if ( this->decode_vtex( std::span<const std::byte>( buf.data( ), buf.size( ) ), *it->second ) )
				{
					it->second->state.store( image_state::decoded, std::memory_order_release );
				}
				else
				{
					it->second->state.store( image_state::failed, std::memory_order_release );
				}
			} );
	}

	bool econ_item_system::finalize_texture( image_entry& entry )
	{
		const auto device = xdraw::device( );
		if ( !device )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		if ( entry.mip_buffers.empty( ) )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		auto upload_format = entry.format;
		std::vector<std::uint8_t> rgba_pixels;

		if ( entry.format == DXGI_FORMAT_BC7_UNORM )
		{
			rgba_pixels.resize( static_cast< std::size_t >( entry.width ) * entry.height * 4 );
			bc7::decode_image( entry.mip_buffers[ 0 ].data( ), rgba_pixels.data( ), static_cast< int >( entry.width ), static_cast< int >( entry.height ) );
			upload_format = DXGI_FORMAT_R8G8B8A8_UNORM;
		}

		const auto upload_data = upload_format == DXGI_FORMAT_R8G8B8A8_UNORM && !rgba_pixels.empty( ) ? rgba_pixels.data( ) : entry.mip_buffers[ 0 ].data( );
		const auto upload_pitch = entry.width * 4;

		D3D11_TEXTURE2D_DESC td{};
		td.Width = entry.width;
		td.Height = entry.height;
		td.MipLevels = 0;
		td.ArraySize = 1;
		td.Format = upload_format;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
		if ( FAILED( device->CreateTexture2D( &td, nullptr, &tex ) ) )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
		device->GetImmediateContext( &ctx );

		ctx->UpdateSubresource( tex.Get( ), 0, nullptr, upload_data, upload_pitch, 0 );

		D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = upload_format;
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = static_cast< UINT >( -1 );

		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		if ( FAILED( device->CreateShaderResourceView( tex.Get( ), &sv, &srv ) ) )
		{
			entry.state.store( image_state::failed, std::memory_order_release );
			return false;
		}

		ctx->GenerateMips( srv.Get( ) );

		entry.image.srv = std::move( srv );
		entry.image.width = static_cast< int >( entry.width );
		entry.image.height = static_cast< int >( entry.height );
		entry.mip_buffers.clear( );
		entry.mip_buffers.shrink_to_fit( );
		entry.state.store( image_state::ready, std::memory_order_release );

		return true;
	}

	econ_item_system::item_category econ_item_system::classify( const char* item_class, int loadout_slot )
	{
		if ( loadout_slot == 38 )
		{
			return item_category::agent;
		}

		if ( loadout_slot == 41 )
		{
			return item_category::glove;
		}

		if ( std::strncmp( item_class, xs( "weapon_knife" ), 12 ) == 0 )
		{
			return item_category::knife;
		}

		if ( std::strncmp( item_class, xs( "weapon_" ), 7 ) == 0 )
		{
			if (
				std::strcmp( item_class, xs( "weapon_flashbang" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_hegrenade" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_smokegrenade" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_molotov" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_decoy" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_incgrenade" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_c4" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_healthshot" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_taser" ) ) == 0 ||
				std::strcmp( item_class, xs( "weapon_knifegg" ) ) == 0
				)
			{
				return item_category::other;
			}

			return item_category::gun;
		}

		return item_category::other;
	}

	std::vector<std::byte> econ_item_system::read_vpk( const std::string& path )
	{
		const auto it = this->m_vpk_index.find( path );
		if ( it == this->m_vpk_index.end( ) )
		{
			return {};
		}

		const auto& entry = it->second;
		auto& stream = this->m_archive_handles[ entry.archive_index ];

		if ( !stream.is_open( ) )
		{
			char archive_name[ 32 ];
			std::snprintf( archive_name, sizeof( archive_name ), xs( "pak01_%03d.vpk" ), entry.archive_index );

			stream.open( this->m_vpk_directory / archive_name, std::ios::binary );
			if ( !stream.is_open( ) )
			{
				return {};
			}
		}

		stream.seekg( entry.offset );

		std::vector<std::byte> data( entry.length );
		stream.read( reinterpret_cast< char* >( data.data( ) ), entry.length );

		return data;
	}

	bool econ_item_system::decode_vtex( std::span<const std::byte> data, image_entry& out )
	{
		const auto raw = reinterpret_cast< const std::uint8_t* >( data.data( ) );
		const auto size = data.size( );

		if ( size < 28 )
		{
			return false;
		}

		const auto file_size = *reinterpret_cast< const std::uint32_t* >( raw + 0x00 );
		const auto header_version = *reinterpret_cast< const std::uint16_t* >( raw + 0x04 );
		const auto block_count = *reinterpret_cast< const std::uint32_t* >( raw + 0x0C );

		if ( header_version != 12 || block_count == 0 || block_count > 64 )
		{
			return false;
		}

		constexpr auto block_header_size{ 16u };
		constexpr auto block_entry_size{ 12u };
		constexpr auto data_fourcc{ 'D' | ( 'A' << 8 ) | ( 'T' << 16 ) | ( 'A' << 24 ) };

		const std::uint8_t* data_block{ nullptr };
		auto data_block_offset{ 0ull };

		for ( auto i = 0u; i < block_count; i++ )
		{
			const auto entry_pos = block_header_size + i * block_entry_size;
			if ( static_cast< std::size_t >( entry_pos ) + block_entry_size > size )
			{
				break;
			}

			const auto type = *reinterpret_cast< const std::uint32_t* >( raw + entry_pos );
			const auto offset = *reinterpret_cast< const std::uint32_t* >( raw + entry_pos + 4 );

			if ( type != data_fourcc )
			{
				continue;
			}

			const auto data_start = entry_pos + 4 + offset;
			if ( static_cast< std::size_t >( data_start ) + 0x28 > size )
			{
				return false;
			}

			data_block = raw + data_start;
			data_block_offset = data_start;
			break;
		}

		if ( !data_block )
		{
			return false;
		}

		const auto width = static_cast< std::uint32_t >( *reinterpret_cast< const std::uint16_t* >( data_block + 0x14 ) );
		const auto height = static_cast< std::uint32_t >( *reinterpret_cast< const std::uint16_t* >( data_block + 0x16 ) );
		const auto format = *reinterpret_cast< const std::uint8_t* >( data_block + 0x1A );
		const auto mip_count = static_cast< std::uint32_t >( *reinterpret_cast< const std::uint8_t* >( data_block + 0x1B ) );
		const auto extra_data_offset = *reinterpret_cast< const std::uint32_t* >( data_block + 0x20 );
		const auto extra_data_count = *reinterpret_cast< const std::uint32_t* >( data_block + 0x24 );

		if ( width == 0 || height == 0 || mip_count == 0 )
		{
			return false;
		}

		const auto pixel_start = static_cast< std::size_t >( file_size );
		if ( pixel_start >= size )
		{
			return false;
		}

		if ( format == 15 || format == 16 || format == 29 ||
			 ( size - pixel_start >= 8 && ( std::memcmp( raw + pixel_start, "\x89PNG\r\n\x1a\n", 8 ) == 0 || std::memcmp( raw + pixel_start, "\xFF\xD8\xFF", 3 ) == 0 ) ) )
		{
			std::vector<std::uint8_t> decoded_pixels;
			std::uint32_t decoded_w{};
			std::uint32_t decoded_h{};

			if ( !decode_image_wic( raw + pixel_start, size - pixel_start, decoded_pixels, decoded_w, decoded_h ) )
			{
				return false;
			}

			out.mip_buffers.clear( );
			out.mip_buffers.push_back( std::move( decoded_pixels ) );
			out.width = decoded_w ? decoded_w : width;
			out.height = decoded_h ? decoded_h : height;
			out.format = DXGI_FORMAT_R8G8B8A8_UNORM;
			return true;
		}

		auto dxgi_format{ DXGI_FORMAT_UNKNOWN };
		auto block_bytes{ 0u };
		auto bytes_per_pixel{ 0u };

		switch ( format )
		{
		case 1:
			dxgi_format = DXGI_FORMAT_BC1_UNORM;
			block_bytes = 8;
			break;
		case 2:
			dxgi_format = DXGI_FORMAT_BC3_UNORM;
			block_bytes = 16;
			break;
		case 4:
			dxgi_format = DXGI_FORMAT_R8G8B8A8_UNORM;
			bytes_per_pixel = 4;
			break;
		case 19:
			dxgi_format = DXGI_FORMAT_BC6H_UF16;
			block_bytes = 16;
			break;
		case 20:
			dxgi_format = DXGI_FORMAT_BC7_UNORM;
			block_bytes = 16;
			break;
		case 27:
			dxgi_format = DXGI_FORMAT_BC4_UNORM;
			block_bytes = 8;
			break;
		case 28:
			dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
			bytes_per_pixel = 4;
			break;
		default:
			return false;
		}

		auto calc_mip_size = [ & ]( std::uint32_t w, std::uint32_t h ) -> std::uint32_t
			{
				if ( block_bytes > 0 )
				{
					const auto bw = std::max( 4u, ( w + 3u ) & ~3u );
					const auto bh = std::max( 4u, ( h + 3u ) & ~3u );
					return ( bw / 4 ) * ( bh / 4 ) * block_bytes;
				}

				return w * h * bytes_per_pixel;
			};

		constexpr auto extra_compressed_mip_size{ 4u };
		auto is_compressed{ false };
		const std::uint32_t* compressed_sizes{ nullptr };
		auto compressed_sizes_count{ 0u };

		if ( extra_data_count > 0 )
		{
			const auto table_pos = static_cast< std::size_t >( 0x20 ) + extra_data_offset;

			for ( auto i = 0u; i < extra_data_count; i++ )
			{
				const auto entry_pos = table_pos + static_cast< std::size_t >( i ) * 12;
				if ( data_block_offset + entry_pos + 12 > size )
				{
					return false;
				}

				const auto etype = *reinterpret_cast< const std::uint32_t* >( data_block + entry_pos );
				const auto eoff = *reinterpret_cast< const std::uint32_t* >( data_block + entry_pos + 4 );
				const auto esize = *reinterpret_cast< const std::uint32_t* >( data_block + entry_pos + 8 );

				if ( etype != extra_compressed_mip_size )
				{
					continue;
				}

				const auto body_pos = entry_pos + 4 + eoff;
				if ( data_block_offset + body_pos + 12 > size || esize < 12 )
				{
					return false;
				}

				const auto int1 = *reinterpret_cast< const std::uint32_t* >( data_block + body_pos );
				const auto mips_offset = *reinterpret_cast< const std::uint32_t* >( data_block + body_pos + 4 );
				const auto mips_count_in_table = *reinterpret_cast< const std::uint32_t* >( data_block + body_pos + 8 );

				if ( int1 > 1 || mips_count_in_table != mip_count )
				{
					return false;
				}

				const auto array_pos = body_pos + 4 + mips_offset;
				if ( data_block_offset + array_pos + mips_count_in_table * 4u > size )
				{
					return false;
				}

				is_compressed = ( int1 == 1 );
				compressed_sizes = reinterpret_cast< const std::uint32_t* >( data_block + array_pos );
				compressed_sizes_count = mips_count_in_table;
				break;
			}
		}

		auto on_disk_size_for = [ & ]( std::uint32_t mip_level ) -> std::uint32_t
			{
				const auto mw = std::max( 1u, width >> mip_level );
				const auto mh = std::max( 1u, height >> mip_level );
				const auto uncompressed = calc_mip_size( mw, mh );

				if ( !is_compressed || compressed_sizes == nullptr || mip_level >= compressed_sizes_count )
				{
					return uncompressed;
				}

				const auto compressed = compressed_sizes[ mip_level ];
				return ( compressed >= uncompressed ) ? uncompressed : compressed;
			};

		std::vector<std::vector<std::uint8_t>> mip_buffers( 1 );
		auto cursor = pixel_start;

		for ( auto j = mip_count; j-- > 0u; )
		{
			const auto on_disk = on_disk_size_for( j );

			if ( cursor + on_disk > size )
			{
				return false;
			}

			if ( j == 0 )
			{
				const auto uncompressed = calc_mip_size( width, height );
				mip_buffers[ 0 ].resize( uncompressed );

				if ( !is_compressed || on_disk >= uncompressed )
				{
					if ( on_disk != uncompressed )
					{
						return false;
					}

					std::memcpy( mip_buffers[ 0 ].data( ), raw + cursor, uncompressed );
				}
				else
				{
					const auto decoded = LZ4_decompress_safe( reinterpret_cast< const char* >( raw + cursor ), reinterpret_cast< char* >( mip_buffers[ 0 ].data( ) ), static_cast< int >( on_disk ), static_cast< int >( uncompressed ) );
					if ( decoded != static_cast< int >( uncompressed ) )
					{
						return false;
					}
				}
			}

			cursor += on_disk;
		}

		out.mip_buffers = std::move( mip_buffers );
		out.width = width;
		out.height = height;
		out.format = dxgi_format;

		return true;
	}

	std::string econ_item_system::build_skin_image_path( const item_def* def, const paint_kit* pk ) const
	{
		if ( !pk || pk->id == 0 )
		{
			return def->image_inventory;
		}

		std::string_view base = def->image_inventory;
		if ( base.empty( ) )
		{
			base = def->name;
		}

		auto slash = base.find_last_of( '/' );
		std::string_view stem = ( slash == std::string_view::npos ) ? base : base.substr( slash + 1 );

		return std::string( xs( "econ/default_generated/" ) ) + std::string( stem ) + "_" + pk->name + xs( "_light" );
	}

} // namespace features::changer
