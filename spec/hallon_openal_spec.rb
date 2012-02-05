require 'bundler/setup'
require 'hallon/openal'

describe Hallon::OpenAL do
  let(:klass)  { described_class }
  let(:format) { Hash.new }
  subject { klass.new }

  describe "#play" do
    it "should not raise an error" do
      expect { subject.play }.to_not raise_error
    end
  end

  describe "#stop" do
    it "should not raise an error" do
      expect { subject.stop }.to_not raise_error
    end
  end

  describe "#pause" do
    it "should not raise an error" do
      expect { subject.pause }.to_not raise_error
    end
  end

  describe "#drops" do
    it "should always return zero" do
      subject.drops.should be_zero
    end
  end

  describe "#format" do
    it "should be settable and gettable" do
      subject.format.should be_nil
      subject.format = format
      subject.format.should eq format
    end
  end
end
