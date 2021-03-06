// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_POLICY_CORE_BROWSER_CONFIGURATION_POLICY_HANDLER_H_
#define COMPONENTS_POLICY_CORE_BROWSER_CONFIGURATION_POLICY_HANDLER_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/values.h"
#include "components/policy/core/common/schema.h"
#include "components/policy/policy_export.h"

class PrefValueMap;

namespace policy {

class PolicyErrorMap;
struct PolicyHandlerParameters;
class PolicyMap;

// Maps a policy type to a preference path, and to the expected value type.
struct POLICY_EXPORT PolicyToPreferenceMapEntry {
  const char* const policy_name;
  const char* const preference_path;
  const base::Value::Type value_type;
};

// An abstract super class that subclasses should implement to map policies to
// their corresponding preferences, and to check whether the policies are valid.
class POLICY_EXPORT ConfigurationPolicyHandler {
 public:
  static std::string ValueTypeToString(base::Value::Type type);

  ConfigurationPolicyHandler();
  virtual ~ConfigurationPolicyHandler();

  // Returns whether the policy settings handled by this
  // ConfigurationPolicyHandler can be applied.  Fills |errors| with error
  // messages or warnings.  |errors| may contain error messages even when
  // |CheckPolicySettings()| returns true.
  virtual bool CheckPolicySettings(const PolicyMap& policies,
                                   PolicyErrorMap* errors) = 0;

  // Processes the policies handled by this ConfigurationPolicyHandler and sets
  // the appropriate preferences in |prefs|.
  virtual void ApplyPolicySettingsWithParameters(
      const PolicyMap& policies,
      const PolicyHandlerParameters& parameters,
      PrefValueMap* prefs);

  // This is a convenience version of ApplyPolicySettingsWithParameters()
  // that leaves out the |parameters|. Anyone extending
  // ConfigurationPolicyHandler should implement either ApplyPolicySettings or
  // ApplyPolicySettingsWithParameters.
  virtual void ApplyPolicySettings(const PolicyMap& policies,
                                   PrefValueMap* prefs);

  // Modifies the values of some of the policies in |policies| so that they
  // are more suitable to display to the user. This can be used to remove
  // sensitive values such as passwords, or to pretty-print values.
  virtual void PrepareForDisplaying(PolicyMap* policies) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyHandler);
};

// Abstract class derived from ConfigurationPolicyHandler that should be
// subclassed to handle a single policy (not a combination of policies).
class POLICY_EXPORT TypeCheckingPolicyHandler
    : public ConfigurationPolicyHandler {
 public:
  TypeCheckingPolicyHandler(const char* policy_name,
                            base::Value::Type value_type);
  virtual ~TypeCheckingPolicyHandler();

  // ConfigurationPolicyHandler methods:
  virtual bool CheckPolicySettings(const PolicyMap& policies,
                                   PolicyErrorMap* errors) OVERRIDE;

  const char* policy_name() const;

 protected:
  // Runs policy checks and returns the policy value if successful.
  bool CheckAndGetValue(const PolicyMap& policies,
                        PolicyErrorMap* errors,
                        const base::Value** value);

 private:
  // The name of the policy.
  const char* policy_name_;

  // The type the value of the policy should have.
  base::Value::Type value_type_;

  DISALLOW_COPY_AND_ASSIGN(TypeCheckingPolicyHandler);
};

// Abstract class derived from TypeCheckingPolicyHandler that ensures an int
// policy's value lies in an allowed range. Either clamps or rejects values
// outside the range.
class POLICY_EXPORT IntRangePolicyHandlerBase
    : public TypeCheckingPolicyHandler {
 public:
  IntRangePolicyHandlerBase(const char* policy_name,
                            int min,
                            int max,
                            bool clamp);

  // ConfigurationPolicyHandler:
  virtual bool CheckPolicySettings(const PolicyMap& policies,
                                   PolicyErrorMap* errors) OVERRIDE;

 protected:
  virtual ~IntRangePolicyHandlerBase();

  // Ensures that the value is in the allowed range. Returns false if the value
  // cannot be parsed or lies outside the allowed range and clamping is
  // disabled.
  bool EnsureInRange(const base::Value* input,
                     int* output,
                     PolicyErrorMap* errors);

 private:
  // The minimum value allowed.
  int min_;

  // The maximum value allowed.
  int max_;

  // Whether to clamp values lying outside the allowed range instead of
  // rejecting them.
  bool clamp_;

  DISALLOW_COPY_AND_ASSIGN(IntRangePolicyHandlerBase);
};

// ConfigurationPolicyHandler for policies that map directly to a preference.
class POLICY_EXPORT SimplePolicyHandler : public TypeCheckingPolicyHandler {
 public:
  SimplePolicyHandler(const char* policy_name,
                      const char* pref_path,
                      base::Value::Type value_type);
  virtual ~SimplePolicyHandler();

  // ConfigurationPolicyHandler methods:
  virtual void ApplyPolicySettings(const PolicyMap& policies,
                                   PrefValueMap* prefs) OVERRIDE;

 private:
  // The DictionaryValue path of the preference the policy maps to.
  const char* pref_path_;

  DISALLOW_COPY_AND_ASSIGN(SimplePolicyHandler);
};

// Base class that encapsulates logic for mapping from a string enum list
// to a separate matching type value.
class POLICY_EXPORT StringMappingListPolicyHandler
    : public TypeCheckingPolicyHandler {
 public:
  // Data structure representing the map between policy strings and
  // matching pref values.
  class POLICY_EXPORT MappingEntry {
   public:
    MappingEntry(const char* policy_value, scoped_ptr<base::Value> map);
    ~MappingEntry();

    const char* enum_value;
    scoped_ptr<base::Value> mapped_value;
  };

  // Callback that generates the map for this instance.
  typedef base::Callback<void(ScopedVector<MappingEntry>*)> GenerateMapCallback;

  StringMappingListPolicyHandler(const char* policy_name,
                                 const char* pref_path,
                                 const GenerateMapCallback& map_generator);
  virtual ~StringMappingListPolicyHandler();

  // ConfigurationPolicyHandler methods:
  virtual bool CheckPolicySettings(const PolicyMap& policies,
                                   PolicyErrorMap* errors) OVERRIDE;
  virtual void ApplyPolicySettings(const PolicyMap& policies,
                                   PrefValueMap* prefs) OVERRIDE;

 private:
  // Attempts to convert the list in |input| to |output| according to the table,
  // returns false on errors.
  bool Convert(const base::Value* input,
               base::ListValue* output,
               PolicyErrorMap* errors);

  // Helper method that converts from a policy value string to the associated
  // pref value.
  scoped_ptr<base::Value> Map(const std::string& entry_value);

  // Name of the pref to write.
  const char* pref_path_;

  // The callback invoked to generate the map for this instance.
  GenerateMapCallback map_getter_;

  // Map of string policy values to local pref values. This is generated lazily
  // so the generation does not have to happen if no policy is present.
  ScopedVector<MappingEntry> map_;

  DISALLOW_COPY_AND_ASSIGN(StringMappingListPolicyHandler);
};

// A policy handler implementation that ensures an int policy's value lies in an
// allowed range.
class POLICY_EXPORT IntRangePolicyHandler : public IntRangePolicyHandlerBase {
 public:
  IntRangePolicyHandler(const char* policy_name,
                        const char* pref_path,
                        int min,
                        int max,
                        bool clamp);
  virtual ~IntRangePolicyHandler();

  // ConfigurationPolicyHandler:
  virtual void ApplyPolicySettings(const PolicyMap& policies,
                                   PrefValueMap* prefs) OVERRIDE;

 private:
  // Name of the pref to write.
  const char* pref_path_;

  DISALLOW_COPY_AND_ASSIGN(IntRangePolicyHandler);
};

// A policy handler implementation that maps an int percentage value to a
// double.
class POLICY_EXPORT IntPercentageToDoublePolicyHandler
    : public IntRangePolicyHandlerBase {
 public:
  IntPercentageToDoublePolicyHandler(const char* policy_name,
                                     const char* pref_path,
                                     int min,
                                     int max,
                                     bool clamp);
  virtual ~IntPercentageToDoublePolicyHandler();

  // ConfigurationPolicyHandler:
  virtual void ApplyPolicySettings(const PolicyMap& policies,
                                   PrefValueMap* prefs) OVERRIDE;

 private:
  // Name of the pref to write.
  const char* pref_path_;

  DISALLOW_COPY_AND_ASSIGN(IntPercentageToDoublePolicyHandler);
};

// Like TypeCheckingPolicyHandler, but validates against a schema instead of a
// single type. |schema| is the schema used for this policy, and |strategy| is
// the strategy used for schema validation errors.
class POLICY_EXPORT SchemaValidatingPolicyHandler
    : public ConfigurationPolicyHandler {
 public:
  SchemaValidatingPolicyHandler(const char* policy_name,
                                Schema schema,
                                SchemaOnErrorStrategy strategy);
  virtual ~SchemaValidatingPolicyHandler();

  // ConfigurationPolicyHandler:
  virtual bool CheckPolicySettings(const PolicyMap& policies,
                                   PolicyErrorMap* errors) OVERRIDE;

  const char* policy_name() const;

 protected:
  // Runs policy checks and returns the policy value if successful.
  bool CheckAndGetValue(const PolicyMap& policies,
                        PolicyErrorMap* errors,
                        scoped_ptr<base::Value>* output);

 private:
  const char* policy_name_;
  Schema schema_;
  SchemaOnErrorStrategy strategy_;

  DISALLOW_COPY_AND_ASSIGN(SchemaValidatingPolicyHandler);
};

// Maps policy to pref like SimplePolicyHandler while ensuring that the value
// set matches the schema. |schema| is the schema used for policies, and
// |strategy| is the strategy used for schema validation errors. The
// |recommended_permission| and |mandatory_permission| flags indicate the levels
// at which the policy can be set. A value set at an unsupported level will be
// ignored.
class POLICY_EXPORT SimpleSchemaValidatingPolicyHandler
    : public SchemaValidatingPolicyHandler {
 public:
  enum MandatoryPermission { MANDATORY_ALLOWED, MANDATORY_PROHIBITED };
  enum RecommendedPermission { RECOMMENDED_ALLOWED, RECOMMENDED_PROHIBITED };

  SimpleSchemaValidatingPolicyHandler(
      const char* policy_name,
      const char* pref_path,
      Schema schema,
      SchemaOnErrorStrategy strategy,
      RecommendedPermission recommended_permission,
      MandatoryPermission mandatory_permission);
  virtual ~SimpleSchemaValidatingPolicyHandler();

  // ConfigurationPolicyHandler:
  virtual bool CheckPolicySettings(const PolicyMap& policies,
                                   PolicyErrorMap* errors) OVERRIDE;
  virtual void ApplyPolicySettings(const PolicyMap& policies,
                                   PrefValueMap* prefs) OVERRIDE;

 private:
  const char* pref_path_;
  const bool allow_recommended_;
  const bool allow_mandatory_;

  DISALLOW_COPY_AND_ASSIGN(SimpleSchemaValidatingPolicyHandler);
};

// A policy handler to deprecate multiple legacy policies with a new one.
// This handler will completely ignore any of legacy policy values if the new
// one is set.
class POLICY_EXPORT LegacyPoliciesDeprecatingPolicyHandler
    : public ConfigurationPolicyHandler {
 public:
  LegacyPoliciesDeprecatingPolicyHandler(
      ScopedVector<ConfigurationPolicyHandler> legacy_policy_handlers,
      scoped_ptr<SchemaValidatingPolicyHandler> new_policy_handler);
  virtual ~LegacyPoliciesDeprecatingPolicyHandler();

  // ConfigurationPolicyHandler:
  virtual bool CheckPolicySettings(const PolicyMap& policies,
                                   PolicyErrorMap* errors) OVERRIDE;
  virtual void ApplyPolicySettings(const PolicyMap& policies,
                                   PrefValueMap* prefs) OVERRIDE;

 private:
  ScopedVector<ConfigurationPolicyHandler> legacy_policy_handlers_;
  scoped_ptr<SchemaValidatingPolicyHandler> new_policy_handler_;

  DISALLOW_COPY_AND_ASSIGN(LegacyPoliciesDeprecatingPolicyHandler);
};

}  // namespace policy

#endif  // COMPONENTS_POLICY_CORE_BROWSER_CONFIGURATION_POLICY_HANDLER_H_
