/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "precompiled.h"
#include "ElementStyle.h"
#include "ElementStyleCache.h"
#include <algorithm>
#include "../../Include/Rocket/Core/ElementDocument.h"
#include "../../Include/Rocket/Core/ElementUtilities.h"
#include "../../Include/Rocket/Core/Log.h"
#include "../../Include/Rocket/Core/Property.h"
#include "../../Include/Rocket/Core/PropertyDefinition.h"
#include "../../Include/Rocket/Core/PropertyDictionary.h"
#include "../../Include/Rocket/Core/StyleSheetSpecification.h"
#include "ElementBackground.h"
#include "ElementBorder.h"
#include "ElementDecoration.h"
#include "ElementDefinition.h"
#include <Rocket/Core/FontFaceHandle.h>

namespace Rocket {
namespace Core {

ElementStyle::ElementStyle(Element* _element)
{
	local_properties = NULL;
	em_properties = NULL;
	definition = NULL;
	element = _element;
	cache = new ElementStyleCache(this);

	definition_dirty = true;
	child_definition_dirty = true;
}

ElementStyle::~ElementStyle()
{
	if (local_properties != NULL)
		delete local_properties;
	if (em_properties != NULL)
		delete em_properties;

	if (definition != NULL)
		definition->RemoveReference();

	delete cache;
}

// Returns the element's definition, updating if necessary.
const ElementDefinition* ElementStyle::GetDefinition()
{
	if (definition_dirty)
	{
		UpdateDefinition();
	}

	return definition;
}
	
void ElementStyle::UpdateDefinition()
{
	if (definition_dirty)
	{
		definition_dirty = false;
		
		ElementDefinition* new_definition = NULL;
		
		const StyleSheet* style_sheet = GetStyleSheet();
		if (style_sheet != NULL)
		{
			new_definition = style_sheet->GetElementDefinition(element);
		}
		
		// Switch the property definitions if the definition has changed.
		if (new_definition != definition || new_definition == NULL)
		{
			PropertyNameList properties;
			
			if (definition != NULL)
			{
				definition->GetDefinedProperties(properties, pseudo_classes);
				definition->RemoveReference();
			}
			
			definition = new_definition;
			
			if (definition != NULL)
				definition->GetDefinedProperties(properties, pseudo_classes);
			
			DirtyProperties(properties);
			element->GetElementDecoration()->ReloadDecorators();
		}
		else if (new_definition != NULL)
		{
			new_definition->RemoveReference();
		}
	}
	
	if (child_definition_dirty)
	{
		for (int i = 0; i < element->GetNumChildren(true); i++)
		{
			element->GetChild(i)->GetStyle()->UpdateDefinition();
		}
		
		child_definition_dirty = false;
	}
}

// Sets or removes a pseudo-class on the element.
void ElementStyle::SetPseudoClass(const String& pseudo_class, bool activate)
{
	size_t num_pseudo_classes = pseudo_classes.size();

	if (activate)
		pseudo_classes.insert(pseudo_class);
	else
		pseudo_classes.erase(pseudo_class);

	if (pseudo_classes.size() != num_pseudo_classes)
	{
		element->GetElementDecoration()->DirtyDecorators();

		const ElementDefinition* definition = element->GetDefinition();
		if (definition != NULL)
		{
			PropertyNameList properties;
			definition->GetDefinedProperties(properties, pseudo_classes, pseudo_class);
			DirtyProperties(properties);

			switch (definition->GetPseudoClassVolatility(pseudo_class))
			{
				case ElementDefinition::FONT_VOLATILE:
					element->DirtyFont();
					break;

				case ElementDefinition::STRUCTURE_VOLATILE:
					DirtyChildDefinitions();
					break;

				default:
					break;
			}
		}
	}
}

// Checks if a specific pseudo-class has been set on the element.
bool ElementStyle::IsPseudoClassSet(const String& pseudo_class) const
{
	return (pseudo_classes.find(pseudo_class) != pseudo_classes.end());
}

const PseudoClassList& ElementStyle::GetActivePseudoClasses() const
{
	return pseudo_classes;
}

// Sets or removes a class on the element.
void ElementStyle::SetClass(const String& class_name, bool activate)
{
	StringList::iterator class_location = std::find(classes.begin(), classes.end(), class_name);

	if (activate)
	{
		if (class_location == classes.end())
		{
			classes.push_back(class_name);
			DirtyDefinition();
		}
	}
	else
	{
		if (class_location != classes.end())
		{
			classes.erase(class_location);
			DirtyDefinition();
		}
	}
}

// Checks if a class is set on the element.
bool ElementStyle::IsClassSet(const String& class_name) const
{
	return std::find(classes.begin(), classes.end(), class_name) != classes.end();
}

// Specifies the entire list of classes for this element. This will replace any others specified.
void ElementStyle::SetClassNames(const String& class_names)
{
	classes.clear();
	StringUtilities::ExpandString(classes, class_names, ' ');
	DirtyDefinition();
}

// Returns the list of classes specified for this element.
String ElementStyle::GetClassNames() const
{
	String class_names;
	for (size_t i = 0; i < classes.size(); i++)
	{
		if (i != 0)
		{
			class_names.Append(" ");
		}
		class_names.Append(classes[i]);
	}

	return class_names;
}

// Sets a local property override on the element.
bool ElementStyle::SetProperty(const String& name, const String& value)
{
	if (local_properties == NULL)
		local_properties = new PropertyDictionary();

	if (StyleSheetSpecification::ParsePropertyDeclaration(*local_properties, name, value))
	{
		DirtyProperty(name);
		return true;
	}
	else
	{
		Log::Message(Log::LT_WARNING, "Syntax error parsing inline property declaration '%s: %s;'.", name.CString(), value.CString());
		return false;
	}
}

// Sets a local property override on the element to a pre-parsed value.
bool ElementStyle::SetProperty(const String& name, const Property& property)
{
	Property new_property = property;

	new_property.definition = StyleSheetSpecification::GetProperty(name);
	if (new_property.definition == NULL)
		return false;

	if (local_properties == NULL)
		local_properties = new PropertyDictionary();

	local_properties->SetProperty(name, new_property);
	DirtyProperty(name);

	return true;
}

// Removes a local property override on the element.
void ElementStyle::RemoveProperty(const String& name)
{
	if (local_properties == NULL)
		return;

	if (local_properties->GetProperty(name) != NULL)
	{
		local_properties->RemoveProperty(name);
		DirtyProperty(name);
	}
}

// Returns one of this element's properties.
const Property* ElementStyle::GetProperty(const String& name)
{
	const Property* local_property = GetLocalProperty(name);
	if (local_property != NULL)
		return local_property;

	// Fetch the property specification.
	const PropertyDefinition* property = StyleSheetSpecification::GetProperty(name);
	if (property == NULL)
		return NULL;

	// If we can inherit this property, return our parent's property.
	if (property->IsInherited())
	{
		Element* parent = element->GetParentNode();
		while (parent != NULL)
		{
			const Property* parent_property = parent->style->GetLocalProperty(name);
			if (parent_property)
				return parent_property;
			
			parent = parent->GetParentNode();
		}
	}

	// No property available! Return the default value.
	return property->GetDefaultValue();
}

// Returns one of this element's properties.
const Property* ElementStyle::GetLocalProperty(const String& name)
{
	// Check for overriding local properties.
	if (local_properties != NULL)
	{
		const Property* property = local_properties->GetProperty(name);
		if (property != NULL)
			return property;
	}

	// Check for a property defined in an RCSS rule.
	if (definition != NULL)
		return definition->GetProperty(name, pseudo_classes);

	return NULL;
}

// Resolves one of this element's properties.
float ElementStyle::ResolveProperty(const Property* property, float base_value)
{
	if (!property)
	{
		ROCKET_ERROR;
		return 0.0f;
	}

	if (property->unit & Property::RELATIVE_UNIT)
	{
		if (property->unit & Property::PERCENT)
			return base_value * property->value.Get< float >() * 0.01f;
		else if (property->unit & Property::EM)
			return property->value.Get< float >() * ElementUtilities::GetFontSize(element);
		else if (property->unit & Property::REM)
			return property->value.Get< float >() * ElementUtilities::GetFontSize(element->GetOwnerDocument());
	}

	if (property->unit & Property::NUMBER || property->unit & Property::PX)
	{
		return property->value.Get< float >();
	}

	// Values based on pixels-per-inch.
	if (property->unit & Property::PPI_UNIT)
	{
		Rocket::Core::RenderInterface* renderInterface = element->GetRenderInterface();
		float inch = property->value.Get< float >() * renderInterface->GetPixelsPerInch();
		
		if (property->unit & Property::INCH) // inch
			return inch;
		if (property->unit & Property::CM) // centimeter
			return inch * (1.0f / 2.54f);
		if (property->unit & Property::MM) // millimeter
			return inch * (1.0f / 25.4f);
		if (property->unit & Property::PT) // point
			return inch * (1.0f / 72.0f);
		if (property->unit & Property::PC) // pica
			return inch * (1.0f / 6.0f);
		if (property->unit & Property::DP) // device-independent pixel
			return (float)Math::RoundUp(inch / renderInterface->GetBasePixelsPerInch());
	}

	// We're not a numeric property; return 0.
	return 0.0f;
}

// Resolves one of this element's properties.
float ElementStyle::ResolveProperty(const String& name, float base_value)
{
	const Property* property = GetProperty(name);
	if (!property)
	{
		ROCKET_ERROR;
		return 0.0f;
	}

	if (property->unit & Property::RELATIVE_UNIT)
	{
		// The calculated value of the font-size property is inherited, so we need to check if this
		// is an inherited property. If so, then we return our parent's font size instead.
		if (name == FONT_SIZE)
		{
			// If the rem unit is used, the font-size is inherited directly from the document,
			// otherwise we use the parent's font size.
			if (property->unit & Property::REM)
			{
				Rocket::Core::ElementDocument* owner_document = element->GetOwnerDocument();
				if (owner_document == NULL)
					return 0;

				base_value = element->GetOwnerDocument()->ResolveProperty(FONT_SIZE, 0);
			}
			else
			{
				Rocket::Core::Element* parent = element->GetParentNode();
				if (parent == NULL)
					return 0;

				if (GetLocalProperty(FONT_SIZE) == NULL)
					return parent->ResolveProperty(FONT_SIZE, 0);

				// The base value for font size is always the height of *this* element's parent's font.
				base_value = parent->ResolveProperty(FONT_SIZE, 0);
			}
		}

		if (property->unit & Property::PERCENT)
			return base_value * property->value.Get< float >() * 0.01f;
		else if (property->unit & Property::EM)
		{
			// If an em-relative font size is specified, it is expressed relative to the parent's
			// font height.
			if (name == FONT_SIZE)
				return property->value.Get< float >() * base_value;
			else
				return property->value.Get< float >() * ElementUtilities::GetFontSize(element);
		}
		else if (property->unit & Property::REM)
		{
			// If an rem-relative font size is specified, it is expressed relative to the document's
			// font height.
			if (name == FONT_SIZE)
				return property->value.Get< float >() * base_value;
			else
				return property->value.Get< float >() * ElementUtilities::GetFontSize(element->GetOwnerDocument());
		}
	}

	if (property->unit & Property::NUMBER || property->unit & Property::PX)
	{
		return property->value.Get< float >();
	}
    
    // Values based on pixels-per-inch.
	if (property->unit & Property::PPI_UNIT)
	{
		Rocket::Core::RenderInterface* renderInterface = element->GetRenderInterface();
		float inch = property->value.Get< float >() * renderInterface->GetPixelsPerInch();

		if (property->unit & Property::INCH) // inch
			return inch;
		if (property->unit & Property::CM) // centimeter
			return inch * (1.0f / 2.54f);
		if (property->unit & Property::MM) // millimeter
			return inch * (1.0f / 25.4f);
		if (property->unit & Property::PT) // point
			return inch * (1.0f / 72.0f);
		if (property->unit & Property::PC) // pica
			return inch * (1.0f / 6.0f);
		if (property->unit & Property::DP) // device-independent pixel
			return (float)Math::RoundUp(inch / renderInterface->GetBasePixelsPerInch());
	}

	// We're not a numeric property; return 0.
	return 0.0f;
}

// Iterates over the properties defined on the element.
bool ElementStyle::IterateProperties(int& index, PseudoClassList& property_pseudo_classes, String& name, const Property*& property)
{
	// First check for locally defined properties.
	if (local_properties != NULL)
	{
		if (index < local_properties->GetNumProperties())
		{
			PropertyMap::const_iterator i = local_properties->GetProperties().begin();
			for (int count = 0; count < index; ++count)
				++i;

			name = (*i).first;
			property = &((*i).second);
			property_pseudo_classes.clear();
			++index;

			return true;
		}
	}

	const ElementDefinition* definition = GetDefinition();
	if (definition != NULL)
	{
		int index_offset = 0;
		if (local_properties != NULL)
			index_offset = local_properties->GetNumProperties();

		// Offset the index to be relative to the definition before we start indexing. When we do get a property back,
		// check that it hasn't been overridden by the element's local properties; if so, continue on to the next one.
		index -= index_offset;
		while (definition->IterateProperties(index, pseudo_classes, property_pseudo_classes, name, property))
		{
			if (local_properties == NULL ||
				local_properties->GetProperty(name) == NULL)
			{
				index += index_offset;
				return true;
			}
		}

		return false;
	}

	return false;
}

// Returns the active style sheet for this element. This may be NULL.
StyleSheet* ElementStyle::GetStyleSheet() const
{
	ElementDocument* document = element->GetOwnerDocument();
	if (document != NULL)
		return document->GetStyleSheet();

	return NULL;
}

void ElementStyle::DirtyDefinition()
{
	definition_dirty = true;
	DirtyChildDefinitions();
	
	// Dirty the child definition update the element tree
	Element* parent = element->GetParentNode();
	while (parent)
	{
		parent->GetStyle()->child_definition_dirty = true;
		parent = parent->GetParentNode();
	}
}

void ElementStyle::DirtyChildDefinitions()
{
	for (int i = 0; i < element->GetNumChildren(true); i++)
		element->GetChild(i)->GetStyle()->DirtyDefinition();
}

// Dirties every property.
void ElementStyle::DirtyProperties()
{
	const PropertyNameList &properties = StyleSheetSpecification::GetRegisteredProperties();
	DirtyProperties(properties);
}

// Dirties em-relative properties.
void ElementStyle::DirtyEmProperties()
{
	const PropertyNameList &properties = StyleSheetSpecification::GetRegisteredProperties();

	if (!em_properties)
	{
		// Check if any of these are currently em-relative. If so, dirty them.
		em_properties = new PropertyNameList;
		for (PropertyNameList::const_iterator list_iterator = properties.begin(); list_iterator != properties.end(); ++list_iterator)
		{
			// Skip font-size; this is relative to our parent's em, not ours.
			if (*list_iterator == FONT_SIZE)
				continue;

			// Get this property from this element. If this is em-relative, then add it to the list to
			// dirty.
			if (element->GetProperty(*list_iterator)->unit == Property::EM)
				em_properties->insert(*list_iterator);
		}
	}

	if (!em_properties->empty())
		DirtyProperties(*em_properties, false);

	// Now dirty all of our descendant's font-size properties that are relative to ems.
	int num_children = element->GetNumChildren(true);
	for (int i = 0; i < num_children; ++i)
		element->GetChild(i)->GetStyle()->DirtyInheritedEmProperties();
}

// Dirties font-size on child elements if appropriate.
void ElementStyle::DirtyInheritedEmProperties()
{
	const Property* font_size = element->GetLocalProperty(FONT_SIZE);
	if (font_size == NULL)
	{
		int num_children = element->GetNumChildren(true);
		for (int i = 0; i < num_children; ++i)
			element->GetChild(i)->GetStyle()->DirtyInheritedEmProperties();
	}
	else
	{
		if (font_size->unit & Property::RELATIVE_UNIT)
			DirtyProperty(FONT_SIZE);
	}
}

// Dirties rem properties.
void ElementStyle::DirtyRemProperties()
{
	const PropertyNameList &properties = StyleSheetSpecification::GetRegisteredProperties();
	PropertyNameList rem_properties;

	// Dirty all the properties of this element that use the rem unit.
	for (PropertyNameList::const_iterator list_iterator = properties.begin(); list_iterator != properties.end(); ++list_iterator)
	{
		if (element->GetProperty(*list_iterator)->unit == Property::REM)
			rem_properties.insert(*list_iterator);
	}

	if (!rem_properties.empty())
		DirtyProperties(rem_properties, false);

	// Now dirty all of our descendant's properties that use the rem unit.
	int num_children = element->GetNumChildren(true);
	for (int i = 0; i < num_children; ++i)
		element->GetChild(i)->GetStyle()->DirtyRemProperties();
}

// Sets a single property as dirty.
void ElementStyle::DirtyProperty(const String& property)
{
	PropertyNameList properties;
	properties.insert(String(property));

	DirtyProperties(properties);
}

// Sets a list of properties as dirty.
void ElementStyle::DirtyProperties(const PropertyNameList& properties, bool clear_em_properties)
{
	if (properties.empty())
		return;

	bool all_inherited_dirty = 
		StyleSheetSpecification::GetRegisteredProperties() == properties ||
		StyleSheetSpecification::GetRegisteredInheritedProperties() == properties;

	if (all_inherited_dirty)
	{
		const PropertyNameList &all_inherited_properties = StyleSheetSpecification::GetRegisteredInheritedProperties();
		for (int i = 0; i < element->GetNumChildren(true); i++)
			element->GetChild(i)->GetStyle()->DirtyInheritedProperties(all_inherited_properties);

		// Clear all cached properties.
		cache->Clear();
	}
	else
	{
		PropertyNameList inherited_properties;

		for (PropertyNameList::const_iterator i = properties.begin(); i != properties.end(); ++i)
		{
			// If this property is an inherited property, then push it into the list to be passed onto our children.
			const PropertyDefinition* property = StyleSheetSpecification::GetProperty(*i);
			if (property != NULL &&
				property->IsInherited())
				inherited_properties.insert(*i);
		}

		// Pass the list of those properties that are inherited onto our children.
		if (!inherited_properties.empty())
		{
			for (int i = 0; i < element->GetNumChildren(true); i++)
				element->GetChild(i)->GetStyle()->DirtyInheritedProperties(inherited_properties);
		}

		// Clear cached properties.
		cache->Clear();
	}

	// clear the list of EM-properties, we will refill it in DirtyEmProperties
	if (clear_em_properties && em_properties != NULL)
	{
		delete em_properties;
		em_properties = NULL;
	}

	// And send the event.
	element->OnPropertyChange(properties);
}

// Sets a list of our potentially inherited properties as dirtied by an ancestor.
void ElementStyle::DirtyInheritedProperties(const PropertyNameList& properties)
{
	bool clear_em_properties = em_properties != NULL;

	PropertyNameList inherited_properties;
	for (PropertyNameList::const_iterator i = properties.begin(); i != properties.end(); ++i)
	{
		const Property *property = GetLocalProperty((*i));
		if (property == NULL)
		{
			inherited_properties.insert(*i);
			if (!clear_em_properties && em_properties != NULL && em_properties->find((*i)) != em_properties->end()) {
				clear_em_properties = true;
			}
		}
	}

	if (inherited_properties.empty())
		return;

	// clear the list of EM-properties, we will refill it in DirtyEmProperties
	if (clear_em_properties && em_properties != NULL)
	{
		delete em_properties;
		em_properties = NULL;
	}

	// Clear cached inherited properties.
	cache->ClearInherited();

	// Pass the list of those properties that this element doesn't override onto our children.
	for (int i = 0; i < element->GetNumChildren(true); i++)
		element->GetChild(i)->GetStyle()->DirtyInheritedProperties(inherited_properties);

	element->OnPropertyChange(properties);
}

void ElementStyle::GetBorderWidthProperties(const Property **border_top_width, const Property **border_bottom_width, const Property **border_left_width, const Property **bottom_right_width)
{
	cache->GetBorderWidthProperties(border_top_width, border_bottom_width, border_left_width, bottom_right_width);
}

void ElementStyle::GetMarginProperties(const Property **margin_top, const Property **margin_bottom, const Property **margin_left, const Property **margin_right)
{
	cache->GetMarginProperties(margin_top, margin_bottom, margin_left, margin_right);
}

void ElementStyle::GetPaddingProperties(const Property **padding_top, const Property **padding_bottom, const Property **padding_left, const Property **padding_right)
{
	cache->GetPaddingProperties(padding_top, padding_bottom, padding_left, padding_right);
}

void ElementStyle::GetDimensionProperties(const Property **width, const Property **height)
{
	cache->GetDimensionProperties(width, height);
}

void ElementStyle::GetLocalDimensionProperties(const Property **width, const Property **height)
{
	cache->GetLocalDimensionProperties(width, height);
}

void ElementStyle::GetOverflow(int *overflow_x, int *overflow_y)
{
	cache->GetOverflow(overflow_x, overflow_y);
}

int ElementStyle::GetPosition()
{
	return cache->GetPosition();
}

int ElementStyle::GetFloat()
{
	return cache->GetFloat();
}

int ElementStyle::GetDisplay()
{
	return cache->GetDisplay();
}

int ElementStyle::GetWhitespace()
{
	return cache->GetWhitespace();
}

const Property *ElementStyle::GetLineHeightProperty()
{
	return cache->GetLineHeightProperty();
}

int ElementStyle::GetTextAlign()
{
	return cache->GetTextAlign();
}

int ElementStyle::GetTextTransform()
{
	return cache->GetTextTransform();
}

const Property *ElementStyle::GetVerticalAlignProperty()
{
	return cache->GetVerticalAlignProperty();
}

}
}
