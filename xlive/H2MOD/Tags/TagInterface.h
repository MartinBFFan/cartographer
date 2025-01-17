#pragma once
#include "..\Blam\Cache\Cache.h"

namespace tags
{
	struct cache_header
	{
		int magic;
		int engine_gen;
		uint32_t file_size;
		int field_C;
		int tag_offset;
		int data_offset;
		int data_size;
		int tag_size;
		int tag_offset_mask;
		int field_24;
		BYTE padding[260];
		char version[32];
		enum scnr_type : int
		{
			SinglePlayer = 0,
			Multiplayer = 1,
			MainMenu = 2,
			MultiplayerShared = 3,
			SinglePlayerShared = 4
		};
		scnr_type type;
		int shared_type;
		int crc_uiid;
		char field_158;
		char tracked__maybe;
		char field_15A;
		char field_15B;
		int field_15C;
		int field_160;
		int field_164;
		int field_168;
		int string_block_offset;
		int string_table_count;
		int string_table_size;
		int string_idx_offset;
		int string_table_offset;
		int extern_deps;
		int time_low;
		int time_high;
		int main_menu_time_low;
		int main_menu_time_high;
		int shared_time_low;
		int shared_time_high;
		int campaign_time_low;
		int campaign_time_high;
		char name[32];
		int field_1C4;
		char tag_name[256];
		int minor_version;
		uint32_t TagNamesCount;
		uint32_t TagNamesBufferOffset;
		uint32_t TagNamesBufferSize;
		uint32_t TagIndicesToName;
		int LanguagePacksOffset;
		int LanguagePacksSize;
		int SecondarySoundGestaltDatumIndex;
		int FastLoadGeometryBlockOffset;
		int FastLoadGeometryBlockSize;
		int Checksum;
		int MoppCodesChecksum;
		BYTE field_2F8[1284];
		int foot;
	};
	static_assert(sizeof(cache_header) == 0x800, "Bad cache header size");

	struct tag_instance
	{
		blam_tag type;
		DatumIndex tag;
		size_t data_offset;
		size_t size;
	};

	struct tag_parent_info
	{
		blam_tag tag;
		blam_tag parent;
		blam_tag grandparent;
	};

	struct tag_offset_header
	{
		void *parent_info;
		int tag_parent_info_count;
		tag_instance *tag_instances;
		DatumIndex scenario_datum;
		DatumIndex globals_datum;
		int field_14;
		int tag_count;
		int type;
	};

	/* 
		Tag Interface

		These functions shouldn't be called while a new cache is being loaded and as such it's not recommended you call them from any thread other than the main one.
		If you want to run code just after a map load register a callback using tags::on_map_load
	*/

	/* Apply required patches to executable */
	void apply_patches();

	/* Register callback on map data load */
	void on_map_load(void (*callback)());

	/* tag data in currently loaded map (merged cache and shared cache data afaik) */
	char *get_tag_data();

	/* header for the current .map/cache file */
	cache_header *get_cache_header();

	/* Returns a handle to the map file currently loaded */
	HANDLE get_cache_handle();

	/* Is a cache loaded? */
	bool cache_file_loaded();

	/* 
		Load tag names from cache file.
		Automatically called on load.
	*/
	bool load_tag_debug_name();

	/* helper function for getting a pointer to data at offset in tag data */
	template <typename T>
	T *get_at_tag_data_offset(size_t offset)
	{
		return reinterpret_cast<T*>(&get_tag_data()[offset]);
	}

	/* header containing information about currently loaded tags */
	inline tag_offset_header *get_tags_header()
	{
		return get_at_tag_data_offset<tag_offset_header>(get_cache_header()->tag_offset_mask);
	}

	/* Returns a pointer to the tag instance array */
	inline tag_instance *get_tag_instances()
	{
		return get_tags_header()->tag_instances;
	}

	/* Returns the number of tags, pretty self explanatory */
	inline long get_tag_count()
	{
		return get_tags_header()->tag_count;
	}

	/* Convert a tag index to a tag datum */
	inline DatumIndex index_to_datum(signed short idx)
	{
		if (idx >= get_tag_count())
		{
			LOG_ERROR_FUNC("Index out of bounds");
			return DatumIndex::Null;
		}
		auto instance = get_tag_instances()[idx];
		DatumIndex tag_datum = instance.tag;
		LOG_CHECK(tag_datum.Index == idx); // should always be true
		return tag_datum;
	}

	/* Get parent tag groups for a tag group */
	inline const tag_parent_info *get_tag_parent_info(const blam_tag &tag_type)
	{
		auto *header = get_tags_header();
		if (!header)
		{
			LOG_ERROR_FUNC("Tags header not loaded");
			return nullptr;
		}
		auto compare_parent_info = [](const void *a, const void *b) -> int
		{
			auto *info_a = static_cast<const tag_parent_info*>(a);
			auto *info_b = static_cast<const tag_parent_info*>(b);
			return info_a->tag.as_int() - info_b->tag.as_int();
		};
		const tag_parent_info search_for{ tag_type, blam_tag::none(), blam_tag::none() };
		return static_cast<tag_parent_info*>(
			bsearch(
				&search_for,
				header->parent_info, 
				header->tag_parent_info_count,
				sizeof(tag_parent_info), 
				compare_parent_info
			));
	}

	/* Returns true if check is the same tag as main or a parent tag */
	inline bool is_tag_or_parent_tag(const blam_tag &main, const blam_tag &check)
	{
		if (main == check)
			return true;
		auto *parent_info = get_tag_parent_info(main);
		if (LOG_CHECK(parent_info))
		{
			if (check == parent_info->tag || check == parent_info->parent || check == parent_info->grandparent)
				return true;
		}
		return false;
	}

	/* 
		gets the name of a tag
		debug names must be loaded or it will fail
	*/
	std::string get_tag_name(DatumIndex tag);

	/* 
		Returns a pointer to a tag, if type is set in template it checks if the real type matches the requested type.
		Returns null on error
	*/
	template <int request_type = 0xFFFFFFFF, typename T = void>
	inline T* get_tag(DatumIndex tag)
	{
		tag_offset_header *header = get_tags_header();

		if (tag.IsNull())
		{
			LOG_ERROR_FUNC("Bad tag datum - null datum", tag.Index, header->tag_count);
			return nullptr;
		}

		// out of bounds check
		if (tag.Index > header->tag_count)
		{
			LOG_CRITICAL_FUNC("Bad tag datum - index out of bounds (idx: {}, bounds: {})", tag.Index, header->tag_count);
			return nullptr;
		}

		tag_instance instance = header->tag_instances[tag.Index];
		if (request_type != 0xFFFFFFFF && !is_tag_or_parent_tag(instance.type, request_type))
		{
			LOG_ERROR_FUNC("tag type doesn't match requested type - to disable check set requested type to 'none' in template");
			return nullptr;
		}

		return get_at_tag_data_offset<T>(instance.data_offset);
	}

	/* 
		Returns the tag datum or a null datum
	*/
	DatumIndex find_tag(blam_tag type, const std::string &name);


	struct ilterator
	{
		ilterator() {};
		ilterator(blam_tag _type) : type(_type) {};

		blam_tag type = blam_tag::none(); // type we are searching for
		long current_index = 0; // current tag idx
		DatumIndex datum = DatumIndex::Null; // last tag datum we returned

		DatumIndex next()
		{
			while (current_index < get_tag_count())
			{
				auto tag_instance = &get_tag_instances()[current_index++];
				if (tag_instance && !tag_instance->type.is_none() && !tag_instance->tag.IsNull())
				{
					if (type.is_none() || is_tag_or_parent_tag(tag_instance->type, type))
					{
						datum = tag_instance->tag;
						return datum;
					}
				}
			}

			return DatumIndex::Null;
		}
	};
}
