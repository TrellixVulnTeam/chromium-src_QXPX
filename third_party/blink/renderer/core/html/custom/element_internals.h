// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_HTML_CUSTOM_ELEMENT_INTERNALS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_HTML_CUSTOM_ELEMENT_INTERNALS_H_

#include "third_party/blink/renderer/bindings/core/v8/file_or_usv_string.h"
#include "third_party/blink/renderer/core/html/forms/listed_element.h"
#include "third_party/blink/renderer/platform/bindings/script_wrappable.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace blink {

class HTMLElement;
class LabelsNodeList;
class ValidityStateFlags;

class ElementInternals : public ScriptWrappable, public ListedElement {
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(ElementInternals);

 public:
  ElementInternals(HTMLElement& target);
  void Trace(Visitor* visitor) override;

  HTMLElement& Target() const { return *target_; }
  void DidUpgrade();

  // IDL attributes/operations
  void setFormValue(const FileOrUSVString& value,
                    ExceptionState& exception_state);
  void setFormValue(const FileOrUSVString& value,
                    FormData* entry_source,
                    ExceptionState& exception_state);
  HTMLFormElement* form(ExceptionState& exception_state) const;
  void setValidity(ValidityStateFlags* flags, ExceptionState& exception_state);
  void setValidity(ValidityStateFlags* flags,
                   const String& message,
                   ExceptionState& exception_state);
  bool willValidate(ExceptionState& exception_state) const;
  ValidityState* validity(ExceptionState& exception_state);
  String ValidationMessageForBinding(ExceptionState& exception_state);
  bool checkValidity(ExceptionState& exception_state);
  bool reportValidity(ExceptionState& exception_state);
  LabelsNodeList* labels();

 private:
  bool IsTargetFormAssociated() const;

  // ListedElement overrides:
  bool IsFormControlElement() const override;
  bool IsElementInternals() const override;
  bool IsEnumeratable() const override;
  void AppendToFormData(FormData& form_data) override;
  void DidChangeForm() override;
  bool HasBadInput() const override;
  bool PatternMismatch() const override;
  bool RangeOverflow() const override;
  bool RangeUnderflow() const override;
  bool StepMismatch() const override;
  bool TooLong() const override;
  bool TooShort() const override;
  bool TypeMismatch() const override;
  bool ValueMissing() const override;
  bool CustomError() const override;
  String validationMessage() const override;
  String ValidationSubMessage() const override;
  void DisabledStateMightBeChanged() override;
  bool ClassSupportsStateRestore() const override;
  bool ShouldSaveAndRestoreFormControlState() const override;
  FormControlState SaveFormControlState() const override;
  void RestoreFormControlState(const FormControlState& state) override;

  Member<HTMLElement> target_;

  FileOrUSVString value_;
  Member<FormData> entry_source_;
  bool is_disabled_ = false;
  Member<ValidityStateFlags> validity_flags_;

  DISALLOW_COPY_AND_ASSIGN(ElementInternals);
};

template <>
struct DowncastTraits<ElementInternals> {
  static bool AllowFrom(const ListedElement& listed_element) {
    return listed_element.IsElementInternals();
  }
};

}  // namespace blink
#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_HTML_CUSTOM_ELEMENT_INTERNALS_H_
